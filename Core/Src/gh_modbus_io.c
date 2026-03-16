#include "gh_runtime_state.h"
#include "gh_modbus_io.h"

#include <string.h>

#define RS485_DE_RE_PORT              GPIOD
#define RS485_DE_RE_PIN               GPIO_PIN_7
#define MODBUS_FUNC_READ_HOLDING      0x03U
#define MODBUS_FUNC_WRITE_SINGLE      0x06U
#define MODBUS_FUNC_WRITE_MULTIPLE    0x10U
#define GH_MODBUS_IO_EVT_TX_DONE      (1UL << 0)
#define GH_MODBUS_IO_EVT_RX_DONE      (1UL << 1)
#define GH_MODBUS_IO_EVT_ERROR        (1UL << 2)

static void modbus_count_tx_error(void)
{
  g_status.crc_errors_tx++;
}

static void modbus_count_rx_error(void)
{
  g_status.crc_errors_rx++;
}

static osMutexId_t s_modbus_io_mutex = NULL;
static osEventFlagsId_t s_modbus_io_events = NULL;
static bool s_modbus_io_ready = false;
static volatile modbus_io_error_t s_last_io_error = MODBUS_IO_ERR_NONE;

static void modbus_set_last_error(modbus_io_error_t err)
{
  s_last_io_error = err;
}

modbus_io_error_t modbus_get_last_error(void)
{
  return s_last_io_error;
}

bool GH_ModbusIo_OnUartTxCplt(UART_HandleTypeDef *huart)
{
  if (!s_modbus_io_ready || (s_modbus_io_events == NULL) || (huart != &huart2))
  {
    return false;
  }

  (void)osEventFlagsSet(s_modbus_io_events, GH_MODBUS_IO_EVT_TX_DONE);
  return true;
}

bool GH_ModbusIo_OnUartRxCplt(UART_HandleTypeDef *huart)
{
  if (!s_modbus_io_ready || (s_modbus_io_events == NULL) || (huart != &huart2))
  {
    return false;
  }

  (void)osEventFlagsSet(s_modbus_io_events, GH_MODBUS_IO_EVT_RX_DONE);
  return true;
}

bool GH_ModbusIo_OnUartError(UART_HandleTypeDef *huart)
{
  if (!s_modbus_io_ready || (s_modbus_io_events == NULL) || (huart != &huart2))
  {
    return false;
  }

  (void)osEventFlagsSet(s_modbus_io_events, GH_MODBUS_IO_EVT_ERROR);
  return true;
}

static uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFU;
  uint16_t i;
  uint8_t j;

  for (i = 0U; i < len; i++)
  {
    crc ^= data[i];
    for (j = 0U; j < 8U; j++)
    {
      if ((crc & 0x0001U) != 0U)
      {
        crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
      }
      else
      {
        crc >>= 1U;
      }
    }
  }
  return crc;
}

static void rs485_set_tx(bool tx_en)
{
  HAL_GPIO_WritePin(RS485_DE_RE_PORT, RS485_DE_RE_PIN, tx_en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool modbus_io_ensure_ready(void)
{
  if (s_modbus_io_ready)
  {
    return true;
  }

  if (osKernelGetState() != osKernelRunning)
  {
    return false;
  }

  s_modbus_io_mutex = osMutexNew(NULL);
  if (s_modbus_io_mutex == NULL)
  {
    return false;
  }

  s_modbus_io_events = osEventFlagsNew(NULL);
  if (s_modbus_io_events == NULL)
  {
    (void)osMutexDelete(s_modbus_io_mutex);
    s_modbus_io_mutex = NULL;
    return false;
  }

  s_modbus_io_ready = true;
  return true;
}

static void uart_drain_rx(UART_HandleTypeDef *huart)
{
  uint8_t b;
  while (HAL_UART_Receive(huart, &b, 1U, 1U) == HAL_OK)
  {
    /* Drain stale bytes to keep Modbus frame boundaries clean. */
  }
}

static void modbus_frame_gap_delay(void)
{
  if (osKernelGetState() == osKernelRunning)
  {
    osDelay(MODBUS_RTU_FRAME_GAP_MS);
  }
}

static bool modbus_uart_tx_blocking(const uint8_t *data, uint16_t len)
{
  uint32_t tc_wait_start_ms;

  rs485_set_tx(true);
  if (HAL_UART_Transmit(&huart2, (uint8_t *)data, len, MODBUS_UART_TX_TIMEOUT_MS) != HAL_OK)
  {
    rs485_set_tx(false);
    modbus_count_tx_error();
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }

  tc_wait_start_ms = HAL_GetTick();
  while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET)
  {
    if ((HAL_GetTick() - tc_wait_start_ms) >= MODBUS_UART_TX_TIMEOUT_MS)
    {
      (void)HAL_UART_AbortTransmit(&huart2);
      rs485_set_tx(false);
      modbus_count_tx_error();
      modbus_set_last_error(MODBUS_IO_ERR_TIMEOUT);
      return false;
    }
  }
  rs485_set_tx(false);
  return true;
}

static bool modbus_uart_rx_it(uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
  uint32_t flags;

  (void)osEventFlagsClear(s_modbus_io_events,
                          GH_MODBUS_IO_EVT_RX_DONE | GH_MODBUS_IO_EVT_ERROR);
  if (HAL_UART_Receive_IT(&huart2, data, len) != HAL_OK)
  {
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }

  flags = osEventFlagsWait(s_modbus_io_events,
                           GH_MODBUS_IO_EVT_RX_DONE | GH_MODBUS_IO_EVT_ERROR,
                           osFlagsWaitAny,
                           timeout_ms);
  if ((flags & osFlagsError) != 0U)
  {
    (void)HAL_UART_AbortReceive(&huart2);
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_TIMEOUT);
    return false;
  }
  if ((flags & GH_MODBUS_IO_EVT_ERROR) != 0U)
  {
    (void)HAL_UART_AbortReceive(&huart2);
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }

  return true;
}

static bool modbus_read_holding_registers_impl(uint8_t slave_id,
                                               uint16_t start_reg,
                                               uint16_t reg_count,
                                               uint16_t *out_regs,
                                               uint32_t timeout_ms)
{
  uint8_t req[8];
  uint8_t resp[5U + (MODBUS_MAX_REGS_PER_REQ * 2U)];
  uint16_t req_crc;
  uint16_t resp_crc;
  uint16_t exp_len;
  uint16_t i;

  modbus_set_last_error(MODBUS_IO_ERR_NONE);
  if ((reg_count == 0U) || (reg_count > MODBUS_MAX_REGS_PER_REQ))
  {
    modbus_set_last_error(MODBUS_IO_ERR_FRAME);
    return false;
  }
  if (!modbus_io_ensure_ready())
  {
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }
  if (osMutexAcquire(s_modbus_io_mutex, osWaitForever) != osOK)
  {
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }

  req[0] = slave_id;
  req[1] = MODBUS_FUNC_READ_HOLDING;
  req[2] = (uint8_t)(start_reg >> 8U);
  req[3] = (uint8_t)(start_reg & 0xFFU);
  req[4] = (uint8_t)(reg_count >> 8U);
  req[5] = (uint8_t)(reg_count & 0xFFU);
  req_crc = modbus_crc16(req, 6U);
  req[6] = (uint8_t)(req_crc & 0xFFU);
  req[7] = (uint8_t)(req_crc >> 8U);

  uart_drain_rx(&huart2);
  if (!modbus_uart_tx_blocking(req, (uint16_t)sizeof(req)))
  {
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  exp_len = (uint16_t)(5U + (reg_count * 2U));
  if (timeout_ms == 0U)
  {
    timeout_ms = MODBUS_RTU_RESP_TIMEOUT_MS;
  }
  if (!modbus_uart_rx_it(resp, exp_len, timeout_ms))
  {
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  if ((resp[0] != slave_id) || (resp[1] != MODBUS_FUNC_READ_HOLDING) || (resp[2] != (uint8_t)(reg_count * 2U)))
  {
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_FRAME);
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  resp_crc = (uint16_t)resp[exp_len - 2U] | ((uint16_t)resp[exp_len - 1U] << 8U);
  if (modbus_crc16(resp, (uint16_t)(exp_len - 2U)) != resp_crc)
  {
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_CRC);
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  for (i = 0U; i < reg_count; i++)
  {
    out_regs[i] = (uint16_t)(((uint16_t)resp[3U + (2U * i)] << 8U) |
                              (uint16_t)resp[3U + (2U * i) + 1U]);
  }
  modbus_frame_gap_delay();
  modbus_set_last_error(MODBUS_IO_ERR_NONE);
  (void)osMutexRelease(s_modbus_io_mutex);
  return true;
}

bool modbus_read_holding_registers_timeout(uint8_t slave_id,
                                           uint16_t start_reg,
                                           uint16_t reg_count,
                                           uint16_t *out_regs,
                                           uint32_t timeout_ms)
{
  return modbus_read_holding_registers_impl(slave_id, start_reg, reg_count, out_regs, timeout_ms);
}

bool modbus_read_holding_registers(uint8_t slave_id,
                                   uint16_t start_reg,
                                   uint16_t reg_count,
                                   uint16_t *out_regs)
{
  return modbus_read_holding_registers_impl(slave_id,
                                            start_reg,
                                            reg_count,
                                            out_regs,
                                            MODBUS_RTU_RESP_TIMEOUT_MS);
}

static bool modbus_write_single_holding_register_impl(uint8_t slave_id,
                                                      uint16_t reg,
                                                      uint16_t value,
                                                      uint32_t timeout_ms)
{
  uint8_t req[8];
  uint8_t resp[8];
  uint16_t crc;
  uint16_t resp_crc;

  modbus_set_last_error(MODBUS_IO_ERR_NONE);
  req[0] = slave_id;
  req[1] = MODBUS_FUNC_WRITE_SINGLE;
  req[2] = (uint8_t)(reg >> 8U);
  req[3] = (uint8_t)(reg & 0xFFU);
  req[4] = (uint8_t)(value >> 8U);
  req[5] = (uint8_t)(value & 0xFFU);
  crc = modbus_crc16(req, 6U);
  req[6] = (uint8_t)(crc & 0xFFU);
  req[7] = (uint8_t)(crc >> 8U);

  if (!modbus_io_ensure_ready())
  {
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }
  if (osMutexAcquire(s_modbus_io_mutex, osWaitForever) != osOK)
  {
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }

  uart_drain_rx(&huart2);
  if (!modbus_uart_tx_blocking(req, (uint16_t)sizeof(req)))
  {
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  if (timeout_ms == 0U)
  {
    timeout_ms = MODBUS_RTU_RESP_TIMEOUT_MS;
  }
  if (!modbus_uart_rx_it(resp, (uint16_t)sizeof(resp), timeout_ms))
  {
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  resp_crc = (uint16_t)resp[6] | ((uint16_t)resp[7] << 8U);
  if ((resp[0] != slave_id) || (resp[1] != MODBUS_FUNC_WRITE_SINGLE) || (resp[2] != req[2]) || (resp[3] != req[3]) ||
      (resp[4] != req[4]) || (resp[5] != req[5]))
  {
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_FRAME);
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }
  if (modbus_crc16(resp, 6U) != resp_crc)
  {
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_CRC);
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }
  modbus_frame_gap_delay();
  modbus_set_last_error(MODBUS_IO_ERR_NONE);
  (void)osMutexRelease(s_modbus_io_mutex);
  return true;
}

bool modbus_write_single_holding_register_timeout(uint8_t slave_id,
                                                  uint16_t reg,
                                                  uint16_t value,
                                                  uint32_t timeout_ms)
{
  return modbus_write_single_holding_register_impl(slave_id, reg, value, timeout_ms);
}

bool modbus_write_single_holding_register(uint8_t slave_id, uint16_t reg, uint16_t value)
{
  return modbus_write_single_holding_register_impl(slave_id, reg, value, MODBUS_RTU_RESP_TIMEOUT_MS);
}

static bool modbus_write_multiple_holding_registers_impl(uint8_t slave_id,
                                                         uint16_t start_reg,
                                                         uint16_t reg_count,
                                                         const uint16_t *regs,
                                                         uint32_t timeout_ms)
{
  uint8_t req[7U + (MODBUS_MAX_REGS_PER_REQ * 2U) + 2U];
  uint8_t resp[8];
  uint16_t crc;
  uint16_t resp_crc;
  uint16_t i;
  uint16_t req_len;

  modbus_set_last_error(MODBUS_IO_ERR_NONE);
  if ((reg_count == 0U) || (reg_count > MODBUS_MAX_REGS_PER_REQ))
  {
    modbus_set_last_error(MODBUS_IO_ERR_FRAME);
    return false;
  }
  if (!modbus_io_ensure_ready())
  {
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }
  if (osMutexAcquire(s_modbus_io_mutex, osWaitForever) != osOK)
  {
    modbus_set_last_error(MODBUS_IO_ERR_UART);
    return false;
  }

  req[0] = slave_id;
  req[1] = MODBUS_FUNC_WRITE_MULTIPLE;
  req[2] = (uint8_t)(start_reg >> 8U);
  req[3] = (uint8_t)(start_reg & 0xFFU);
  req[4] = (uint8_t)(reg_count >> 8U);
  req[5] = (uint8_t)(reg_count & 0xFFU);
  req[6] = (uint8_t)(reg_count * 2U);
  for (i = 0U; i < reg_count; i++)
  {
    req[7U + (2U * i)] = (uint8_t)(regs[i] >> 8U);
    req[7U + (2U * i) + 1U] = (uint8_t)(regs[i] & 0xFFU);
  }
  req_len = (uint16_t)(7U + (2U * reg_count));
  crc = modbus_crc16(req, req_len);
  req[req_len] = (uint8_t)(crc & 0xFFU);
  req[req_len + 1U] = (uint8_t)(crc >> 8U);

  uart_drain_rx(&huart2);
  if (!modbus_uart_tx_blocking(req, (uint16_t)(req_len + 2U)))
  {
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  if (timeout_ms == 0U)
  {
    timeout_ms = MODBUS_RTU_RESP_TIMEOUT_MS;
  }
  if (!modbus_uart_rx_it(resp, (uint16_t)sizeof(resp), timeout_ms))
  {
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  resp_crc = (uint16_t)resp[6] | ((uint16_t)resp[7] << 8U);
  if ((resp[0] != slave_id) || (resp[1] != MODBUS_FUNC_WRITE_MULTIPLE) ||
      (resp[2] != req[2]) || (resp[3] != req[3]) ||
      (resp[4] != req[4]) || (resp[5] != req[5]))
  {
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_FRAME);
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }
  if (modbus_crc16(resp, 6U) != resp_crc)
  {
    modbus_count_rx_error();
    modbus_set_last_error(MODBUS_IO_ERR_CRC);
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }
  modbus_frame_gap_delay();
  modbus_set_last_error(MODBUS_IO_ERR_NONE);
  (void)osMutexRelease(s_modbus_io_mutex);
  return true;
}

bool modbus_write_multiple_holding_registers_timeout(uint8_t slave_id,
                                                     uint16_t start_reg,
                                                     uint16_t reg_count,
                                                     const uint16_t *regs,
                                                     uint32_t timeout_ms)
{
  return modbus_write_multiple_holding_registers_impl(slave_id,
                                                      start_reg,
                                                      reg_count,
                                                      regs,
                                                      timeout_ms);
}

bool modbus_write_multiple_holding_registers(uint8_t slave_id,
                                             uint16_t start_reg,
                                             uint16_t reg_count,
                                             const uint16_t *regs)
{
  return modbus_write_multiple_holding_registers_impl(slave_id,
                                                      start_reg,
                                                      reg_count,
                                                      regs,
                                                      MODBUS_RTU_RESP_TIMEOUT_MS);
}
