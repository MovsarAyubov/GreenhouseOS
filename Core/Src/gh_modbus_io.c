#include "gh_runtime_state.h"
#include "gh_modbus_io.h"

#include "gh_crc32.h"

#include <string.h>
#include <math.h>

#define RS485_DE_RE_PORT              GPIOD
#define RS485_DE_RE_PIN               GPIO_PIN_7
#define MODBUS_FUNC_READ_HOLDING      0x03U
#define MODBUS_FUNC_WRITE_SINGLE      0x06U
#define MODBUS_FUNC_WRITE_MULTIPLE    0x10U
#define MODBUS_CTRL_BASE              100U
#define MODBUS_CTRL_END               121U
#define MODBUS_CTRL_APPLY_CMD         122U
#define MODBUS_CTRL_CRC_LO            123U
#define MODBUS_CTRL_APPLY_STATUS      125U
#define MODBUS_LIGHT_STAGE1_REG       134U
#define MODBUS_LIGHT_STAGE2_REG       135U
#define MODBUS_CTRL_REG_COUNT         ((MODBUS_CTRL_END - MODBUS_CTRL_BASE) + 1U)
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

static bool modbus_uart_tx_it(const uint8_t *data, uint16_t len)
{
  uint32_t flags;
  uint32_t tc_wait_start_ms;

  (void)osEventFlagsClear(s_modbus_io_events,
                          GH_MODBUS_IO_EVT_TX_DONE | GH_MODBUS_IO_EVT_ERROR);

  rs485_set_tx(true);
  if (HAL_UART_Transmit_IT(&huart2, (uint8_t *)data, len) != HAL_OK)
  {
    rs485_set_tx(false);
    modbus_count_tx_error();
    return false;
  }

  flags = osEventFlagsWait(s_modbus_io_events,
                           GH_MODBUS_IO_EVT_TX_DONE | GH_MODBUS_IO_EVT_ERROR,
                           osFlagsWaitAny,
                           MODBUS_UART_TX_TIMEOUT_MS);
  if ((flags & osFlagsError) != 0U)
  {
    (void)HAL_UART_AbortTransmit(&huart2);
    rs485_set_tx(false);
    modbus_count_tx_error();
    return false;
  }
  if ((flags & GH_MODBUS_IO_EVT_ERROR) != 0U)
  {
    (void)HAL_UART_AbortTransmit(&huart2);
    rs485_set_tx(false);
    modbus_count_tx_error();
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
    return false;
  }
  if ((flags & GH_MODBUS_IO_EVT_ERROR) != 0U)
  {
    (void)HAL_UART_AbortReceive(&huart2);
    modbus_count_rx_error();
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

  if ((reg_count == 0U) || (reg_count > MODBUS_MAX_REGS_PER_REQ))
  {
    return false;
  }
  if (!modbus_io_ensure_ready())
  {
    return false;
  }
  if (osMutexAcquire(s_modbus_io_mutex, osWaitForever) != osOK)
  {
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
  if (!modbus_uart_tx_it(req, (uint16_t)sizeof(req)))
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
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  resp_crc = (uint16_t)resp[exp_len - 2U] | ((uint16_t)resp[exp_len - 1U] << 8U);
  if (modbus_crc16(resp, (uint16_t)(exp_len - 2U)) != resp_crc)
  {
    modbus_count_rx_error();
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }

  for (i = 0U; i < reg_count; i++)
  {
    out_regs[i] = (uint16_t)(((uint16_t)resp[3U + (2U * i)] << 8U) |
                              (uint16_t)resp[3U + (2U * i) + 1U]);
  }
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
    return false;
  }
  if (osMutexAcquire(s_modbus_io_mutex, osWaitForever) != osOK)
  {
    return false;
  }

  uart_drain_rx(&huart2);
  if (!modbus_uart_tx_it(req, (uint16_t)sizeof(req)))
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
      (resp[4] != req[4]) || (resp[5] != req[5]) ||
      (modbus_crc16(resp, 6U) != resp_crc))
  {
    modbus_count_rx_error();
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }
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

  if ((reg_count == 0U) || (reg_count > MODBUS_MAX_REGS_PER_REQ))
  {
    return false;
  }
  if (!modbus_io_ensure_ready())
  {
    return false;
  }
  if (osMutexAcquire(s_modbus_io_mutex, osWaitForever) != osOK)
  {
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
  if (!modbus_uart_tx_it(req, (uint16_t)(req_len + 2U)))
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
      (resp[4] != req[4]) || (resp[5] != req[5]) ||
      (modbus_crc16(resp, 6U) != resp_crc))
  {
    modbus_count_rx_error();
    (void)osMutexRelease(s_modbus_io_mutex);
    return false;
  }
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

static uint16_t cfg_u16_clamped(float v, uint16_t min_v, uint16_t max_v)
{
  int32_t iv = (int32_t)lroundf(v);
  if (iv < (int32_t)min_v)
  {
    iv = (int32_t)min_v;
  }
  if (iv > (int32_t)max_v)
  {
    iv = (int32_t)max_v;
  }
  return (uint16_t)iv;
}

static bool hhmm_is_valid(uint16_t hhmm)
{
  uint16_t hh = (uint16_t)(hhmm / 100U);
  uint16_t mm = (uint16_t)(hhmm % 100U);
  return (hh < 24U) && (mm < 60U);
}

static uint32_t control_regs_crc32(const uint16_t *regs, uint16_t count)
{
  uint8_t bytes[MODBUS_CTRL_REG_COUNT * 2U];
  uint16_t i;
  for (i = 0U; i < count; i++)
  {
    bytes[2U * i] = (uint8_t)(regs[i] >> 8U);
    bytes[(2U * i) + 1U] = (uint8_t)(regs[i] & 0xFFU);
  }
  return gh_crc32_compute(bytes, (uint32_t)(count * 2U));
}

static void build_control_regs_from_config(const active_config_t *cfg,
                                           uint16_t *ctrl_regs,
                                           uint16_t *stage1,
                                           uint16_t *stage2)
{
  float f[32] = {0.0f};
  uint32_t i;
  for (i = 0U; i < 32U; i++)
  {
    memcpy(&f[i], &cfg->payload[i * sizeof(float)], sizeof(float));
  }

  memset(ctrl_regs, 0, sizeof(uint16_t) * MODBUS_CTRL_REG_COUNT);
  ctrl_regs[0] = (uint16_t)((cfg->version >> 16U) & 0xFFFFU);
  ctrl_regs[1] = (uint16_t)(cfg->version & 0xFFFFU);
  ctrl_regs[2] = 0U;
  ctrl_regs[3] = cfg_u16_clamped(f[0] * 10.0f, 0U, 1000U);
  ctrl_regs[4] = cfg_u16_clamped(f[1] * 10.0f, 0U, 1000U);
  ctrl_regs[5] = cfg_u16_clamped(f[2] * 10.0f, 0U, 1000U);
  ctrl_regs[6] = cfg_u16_clamped(f[3] * 10.0f, 0U, 2000U);
  ctrl_regs[7] = cfg_u16_clamped(f[4] * 10.0f, 0U, 2000U);
  ctrl_regs[8] = cfg_u16_clamped(f[5] * 10.0f, 0U, 2000U);
  ctrl_regs[9] = cfg_u16_clamped(f[6] * 10.0f, 0U, 2000U);

  for (i = 0U; i < 4U; i++)
  {
    uint16_t en = (cfg_u16_clamped(f[7U + (3U * i)], 0U, 1U) > 0U) ? 1U : 0U;
    uint16_t on_hhmm = cfg_u16_clamped(f[8U + (3U * i)], 0U, 2359U);
    uint16_t off_hhmm = cfg_u16_clamped(f[9U + (3U * i)], 0U, 2359U);
    if ((!hhmm_is_valid(on_hhmm)) || (!hhmm_is_valid(off_hhmm)))
    {
      en = 0U;
      on_hhmm = 0U;
      off_hhmm = 0U;
    }
    ctrl_regs[10U + (3U * i)] = en;
    ctrl_regs[11U + (3U * i)] = on_hhmm;
    ctrl_regs[12U + (3U * i)] = off_hhmm;
  }

  *stage1 = (cfg_u16_clamped(f[19], 0U, 1U) > 0U) ? 1U : 0U;
  *stage2 = (cfg_u16_clamped(f[20], 0U, 1U) > 0U) ? 1U : 0U;
}

bool apply_control_to_slave(uint8_t slave_id, const active_config_t *cfg)
{
  uint16_t ctrl_regs[MODBUS_CTRL_REG_COUNT];
  uint16_t crc_regs[2];
  uint16_t st1 = 0U;
  uint16_t st2 = 0U;
  uint16_t apply_regs[3] = {0U};
  uint16_t diag_regs[MODBUS_DIAG_REG_COUNT] = {0U};
  uint32_t crc;
  bool ok;
  uint32_t active_ver;

  build_control_regs_from_config(cfg, ctrl_regs, &st1, &st2);
  ok = modbus_write_multiple_holding_registers(slave_id, MODBUS_CTRL_BASE, MODBUS_CTRL_REG_COUNT, ctrl_regs);
  if (!ok)
  {
    return false;
  }

  crc = control_regs_crc32(ctrl_regs, MODBUS_CTRL_REG_COUNT);
  crc_regs[0] = (uint16_t)(crc & 0xFFFFU);
  crc_regs[1] = (uint16_t)((crc >> 16U) & 0xFFFFU);
  ok = modbus_write_multiple_holding_registers(slave_id, MODBUS_CTRL_CRC_LO, 2U, crc_regs);
  ok = ok && modbus_write_single_holding_register(slave_id, MODBUS_LIGHT_STAGE1_REG, st1);
  ok = ok && modbus_write_single_holding_register(slave_id, MODBUS_LIGHT_STAGE2_REG, st2);
  ok = ok && modbus_write_single_holding_register(slave_id, MODBUS_CTRL_APPLY_CMD, 1U);
  if (!ok)
  {
    return false;
  }

  ok = modbus_read_holding_registers(slave_id, MODBUS_CTRL_APPLY_STATUS, 3U, apply_regs);
  if (!ok)
  {
    return false;
  }
  g_status.last_apply_status = (uint8_t)(apply_regs[0] & 0x00FFU);
  active_ver = ((uint32_t)apply_regs[1] << 16U) | (uint32_t)apply_regs[2];
  if (active_ver != cfg->version)
  {
    return false;
  }

  ok = modbus_read_holding_registers(slave_id, MODBUS_DIAG_BASE, MODBUS_DIAG_REG_COUNT, diag_regs);
  if (ok)
  {
    g_status.control_mode = (uint8_t)(diag_regs[0] & 0x00FFU);
    g_status.autonomous_reason = (uint8_t)(diag_regs[1] & 0x00FFU);
    g_status.last_master_seen_ms = ((uint32_t)diag_regs[2] << 16U) | (uint32_t)diag_regs[3];
    g_status.good_cycle_streak = diag_regs[4];
    g_status.last_apply_status = (uint8_t)(diag_regs[5] & 0x00FFU);
  }

  return true;
}
