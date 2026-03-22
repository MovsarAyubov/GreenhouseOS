#include "gh_modbus_tcp_server.h"

#include "gh_modbus_map.h"
#include "gh_runtime_state.h"
#include "cmsis_os.h"
#include "lwip.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netif.h"
#include "main.h"
#include "task.h"

#include <stdbool.h>
#include <string.h>

#define GH_TCP_NETIF_WAIT_STEP_MS        100U
#define GH_TCP_NETIF_WAIT_TIMEOUT_MS   10000U
#define GH_TCP_LISTENER_TIMEOUT_MS      100U
#define GH_TCP_CLIENT_TIMEOUT_MS        100U
#define GH_TCP_PARTIAL_FRAME_TIMEOUT_MS 1000U
#define GH_TCP_SERVER_PORT              502U
#define GH_TCP_RX_BUFFER_SIZE           512U
#define GH_TCP_TX_BUFFER_SIZE           512U
#define GH_TCP_MBAP_HEADER_SIZE         7U
#define GH_TCP_MAX_MBAP_LENGTH          253U
#define GH_TCP_MAX_READ_REGS            125U
#define GH_TCP_MAX_WRITE_REGS           123U

#ifdef TCP_SERVER_IDLE_TIMEOUT_MS
#define GH_TCP_IDLE_TIMEOUT_MS ((uint32_t)TCP_SERVER_IDLE_TIMEOUT_MS)
#else
#define GH_TCP_IDLE_TIMEOUT_MS 60000U
#endif

#define GH_MB_FC_READ_HOLDING           0x03U
#define GH_MB_FC_WRITE_SINGLE           0x06U
#define GH_MB_FC_WRITE_MULTIPLE         0x10U

#define GH_MB_EX_ILLEGAL_FUNCTION       0x01U
#define GH_MB_EX_ILLEGAL_DATA_ADDRESS   0x02U
#define GH_MB_EX_ILLEGAL_DATA_VALUE     0x03U
#define GH_MB_EX_SERVER_DEVICE_FAILURE  0x04U

typedef struct
{
  struct netconn *listener;
  struct netconn *client;
  uint16_t rx_len;
  uint8_t rx_buffer[GH_TCP_RX_BUFFER_SIZE];
  uint8_t tx_buffer[GH_TCP_TX_BUFFER_SIZE];
  uint16_t reg_scratch[GH_TCP_MAX_READ_REGS];
  uint32_t last_activity_ms;
  uint32_t partial_frame_start_ms;
  TaskHandle_t task_handle;
  gh_modbus_tcp_diag_t diag;
  bool link_ready;
  bool link_down_reported;
  uint32_t netif_wait_start_ms;
} gh_modbus_tcp_server_state_t;

static gh_modbus_tcp_server_state_t s_tcp = {0};
extern struct netif gnetif;

static uint16_t gh_tcp_get_u16_be(const uint8_t *src)
{
  return (uint16_t)(((uint16_t)src[0] << 8U) | (uint16_t)src[1]);
}

static void gh_tcp_set_u16_be(uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t)(value >> 8U);
  dst[1] = (uint8_t)(value & 0x00FFU);
}

static bool gh_tcp_network_ready(void)
{
  return (netif_is_up(&gnetif) != 0) && (netif_is_link_up(&gnetif) != 0);
}

static void gh_tcp_diag_note_recv_err(int32_t err)
{
  s_tcp.diag.lastRecvErr = err;
  s_tcp.diag.lastSendErr = 0;
}

static void gh_tcp_diag_note_send_err(int32_t err)
{
  s_tcp.diag.lastSendErr = err;
  s_tcp.diag.lastRecvErr = 0;
}

static void gh_tcp_reset_rx_state(void)
{
  s_tcp.rx_len = 0U;
  s_tcp.partial_frame_start_ms = 0U;
}

static void gh_tcp_close_client(void)
{
  if (s_tcp.client != NULL)
  {
    netconn_close(s_tcp.client);
    netconn_delete(s_tcp.client);
    s_tcp.client = NULL;
    g_status.tcp_disconnect_count++;
  }
  gh_tcp_reset_rx_state();
}

static void gh_tcp_close_listener(void)
{
  gh_tcp_close_client();
  if (s_tcp.listener != NULL)
  {
    netconn_close(s_tcp.listener);
    netconn_delete(s_tcp.listener);
    s_tcp.listener = NULL;
  }
}

static void gh_tcp_note_link_up(void)
{
  if (!s_tcp.link_ready)
  {
    s_tcp.link_ready = true;
    g_status.link_up_count++;
  }

  s_tcp.netif_wait_start_ms = 0U;
  if (s_tcp.link_down_reported)
  {
    s_tcp.link_down_reported = false;
    if (g_status.last_error_code == EVENT_CODE_LINK_DOWN)
    {
      g_status.last_error_code = 0U;
    }
    publish_event(EVENT_SEV_INFO, EVENT_CODE_LINK_UP, 0U, 0.0f);
  }
}

static void gh_tcp_note_link_down(void)
{
  gh_tcp_close_listener();

  if (s_tcp.link_ready)
  {
    s_tcp.link_ready = false;
    g_status.link_down_count++;
  }

  if (s_tcp.netif_wait_start_ms == 0U)
  {
    s_tcp.netif_wait_start_ms = HAL_GetTick();
  }
  else if (!s_tcp.link_down_reported &&
           ((HAL_GetTick() - s_tcp.netif_wait_start_ms) >= GH_TCP_NETIF_WAIT_TIMEOUT_MS))
  {
    s_tcp.link_down_reported = true;
    g_status.last_error_code = EVENT_CODE_LINK_DOWN;
    publish_event(EVENT_SEV_WARN, EVENT_CODE_LINK_DOWN, 0U, 0.0f);
  }
}

static bool gh_tcp_ensure_listener(void)
{
  err_t err;

  if (s_tcp.listener != NULL)
  {
    return true;
  }

  s_tcp.listener = netconn_new(NETCONN_TCP);
  if (s_tcp.listener == NULL)
  {
    s_tcp.diag.acceptErrCount++;
    gh_tcp_diag_note_recv_err((int32_t)ERR_MEM);
    return false;
  }

  err = netconn_bind(s_tcp.listener, NULL, GH_TCP_SERVER_PORT);
  if (err != ERR_OK)
  {
    s_tcp.diag.acceptErrCount++;
    gh_tcp_diag_note_recv_err((int32_t)err);
    gh_tcp_close_listener();
    return false;
  }

  err = netconn_listen(s_tcp.listener);
  if (err != ERR_OK)
  {
    s_tcp.diag.acceptErrCount++;
    gh_tcp_diag_note_recv_err((int32_t)err);
    gh_tcp_close_listener();
    return false;
  }

  netconn_set_recvtimeout(s_tcp.listener, GH_TCP_LISTENER_TIMEOUT_MS);
  return true;
}

static bool gh_tcp_append_netbuf(struct netbuf *inbuf)
{
  uint16_t copy_len;
  uint16_t copied;

  if ((inbuf == NULL) || (inbuf->p == NULL))
  {
    return false;
  }

  copy_len = (uint16_t)netbuf_len(inbuf);
  if (copy_len == 0U)
  {
    return true;
  }
  if ((uint32_t)s_tcp.rx_len + (uint32_t)copy_len > (uint32_t)sizeof(s_tcp.rx_buffer))
  {
    return false;
  }

  copied = netbuf_copy(inbuf, &s_tcp.rx_buffer[s_tcp.rx_len], copy_len);
  if (copied != copy_len)
  {
    return false;
  }

  s_tcp.rx_len = (uint16_t)(s_tcp.rx_len + copy_len);
  s_tcp.last_activity_ms = HAL_GetTick();
  if ((s_tcp.rx_len > 0U) && (s_tcp.partial_frame_start_ms == 0U))
  {
    s_tcp.partial_frame_start_ms = s_tcp.last_activity_ms;
  }
  return true;
}

static uint16_t gh_tcp_normalize_address(uint16_t address)
{
  if (address >= 41000U)
  {
    return (uint16_t)(address - 41000U);
  }
  return address;
}

static uint16_t gh_tcp_build_exception_pdu(uint8_t function_code, uint8_t exception_code)
{
  s_tcp.tx_buffer[GH_TCP_MBAP_HEADER_SIZE + 0U] = (uint8_t)(function_code | 0x80U);
  s_tcp.tx_buffer[GH_TCP_MBAP_HEADER_SIZE + 1U] = exception_code;
  return 2U;
}

static bool gh_tcp_send_pdu(uint16_t transaction_id, uint8_t unit_id, uint16_t pdu_len)
{
  err_t err;
  uint16_t mbap_length;
  uint16_t frame_len;

  if (s_tcp.client == NULL)
  {
    return false;
  }

  mbap_length = (uint16_t)(pdu_len + 1U);
  frame_len = (uint16_t)(GH_TCP_MBAP_HEADER_SIZE + pdu_len);
  if (frame_len > (uint16_t)sizeof(s_tcp.tx_buffer))
  {
    s_tcp.diag.sendErrCount++;
    gh_tcp_diag_note_send_err((int32_t)ERR_BUF);
    gh_tcp_close_client();
    return false;
  }

  gh_tcp_set_u16_be(&s_tcp.tx_buffer[0], transaction_id);
  s_tcp.tx_buffer[2] = 0U;
  s_tcp.tx_buffer[3] = 0U;
  gh_tcp_set_u16_be(&s_tcp.tx_buffer[4], mbap_length);
  s_tcp.tx_buffer[6] = unit_id;

  err = netconn_write(s_tcp.client, s_tcp.tx_buffer, frame_len, NETCONN_COPY);
  if (err != ERR_OK)
  {
    s_tcp.diag.sendErrCount++;
    gh_tcp_diag_note_send_err((int32_t)err);
    gh_tcp_close_client();
    return false;
  }

  s_tcp.last_activity_ms = HAL_GetTick();
  return true;
}

static bool gh_tcp_handle_fc3(uint16_t transaction_id,
                              uint8_t unit_id,
                              const uint8_t *pdu,
                              uint16_t pdu_len)
{
  uint16_t address;
  uint16_t qty;
  uint16_t i;
  uint16_t tx_pdu_len;

  if (pdu_len != 5U)
  {
    return gh_tcp_send_pdu(transaction_id,
                           unit_id,
                           gh_tcp_build_exception_pdu(GH_MB_FC_READ_HOLDING, GH_MB_EX_ILLEGAL_DATA_VALUE));
  }

  address = gh_tcp_normalize_address(gh_tcp_get_u16_be(&pdu[1]));
  qty = gh_tcp_get_u16_be(&pdu[3]);
  if ((qty == 0U) || (qty > GH_TCP_MAX_READ_REGS))
  {
    return gh_tcp_send_pdu(transaction_id,
                           unit_id,
                           gh_tcp_build_exception_pdu(GH_MB_FC_READ_HOLDING, GH_MB_EX_ILLEGAL_DATA_VALUE));
  }

  if (!GH_ModbusMap_ReadRange(address, qty, s_tcp.reg_scratch))
  {
    return gh_tcp_send_pdu(transaction_id,
                           unit_id,
                           gh_tcp_build_exception_pdu(GH_MB_FC_READ_HOLDING, GH_MB_EX_ILLEGAL_DATA_ADDRESS));
  }

  s_tcp.tx_buffer[GH_TCP_MBAP_HEADER_SIZE + 0U] = GH_MB_FC_READ_HOLDING;
  s_tcp.tx_buffer[GH_TCP_MBAP_HEADER_SIZE + 1U] = (uint8_t)(qty * 2U);
  tx_pdu_len = (uint16_t)(2U + qty * 2U);
  for (i = 0U; i < qty; i++)
  {
    gh_tcp_set_u16_be(&s_tcp.tx_buffer[GH_TCP_MBAP_HEADER_SIZE + 2U + (i * 2U)], s_tcp.reg_scratch[i]);
  }

  return gh_tcp_send_pdu(transaction_id, unit_id, tx_pdu_len);
}

static bool gh_tcp_handle_fc6(uint16_t transaction_id,
                              uint8_t unit_id,
                              const uint8_t *pdu,
                              uint16_t pdu_len)
{
  uint16_t address;
  uint16_t value;

  if (pdu_len != 5U)
  {
    return gh_tcp_send_pdu(transaction_id,
                           unit_id,
                           gh_tcp_build_exception_pdu(GH_MB_FC_WRITE_SINGLE, GH_MB_EX_ILLEGAL_DATA_VALUE));
  }

  address = gh_tcp_normalize_address(gh_tcp_get_u16_be(&pdu[1]));
  value = gh_tcp_get_u16_be(&pdu[3]);
  if (!GH_ModbusMap_WriteSingle(address, value))
  {
    return gh_tcp_send_pdu(transaction_id,
                           unit_id,
                           gh_tcp_build_exception_pdu(GH_MB_FC_WRITE_SINGLE, GH_MB_EX_ILLEGAL_DATA_ADDRESS));
  }

  memcpy(&s_tcp.tx_buffer[GH_TCP_MBAP_HEADER_SIZE], pdu, pdu_len);
  return gh_tcp_send_pdu(transaction_id, unit_id, pdu_len);
}

static bool gh_tcp_handle_fc16(uint16_t transaction_id,
                               uint8_t unit_id,
                               const uint8_t *pdu,
                               uint16_t pdu_len)
{
  uint16_t address;
  uint16_t qty;
  uint16_t byte_count;
  uint16_t i;

  if (pdu_len < 6U)
  {
    return gh_tcp_send_pdu(transaction_id,
                           unit_id,
                           gh_tcp_build_exception_pdu(GH_MB_FC_WRITE_MULTIPLE, GH_MB_EX_ILLEGAL_DATA_VALUE));
  }

  address = gh_tcp_normalize_address(gh_tcp_get_u16_be(&pdu[1]));
  qty = gh_tcp_get_u16_be(&pdu[3]);
  byte_count = (uint16_t)pdu[5];
  if ((qty == 0U) || (qty > GH_TCP_MAX_WRITE_REGS) || (byte_count != (uint16_t)(qty * 2U)) ||
      (pdu_len != (uint16_t)(6U + byte_count)))
  {
    return gh_tcp_send_pdu(transaction_id,
                           unit_id,
                           gh_tcp_build_exception_pdu(GH_MB_FC_WRITE_MULTIPLE, GH_MB_EX_ILLEGAL_DATA_VALUE));
  }

  for (i = 0U; i < qty; i++)
  {
    s_tcp.reg_scratch[i] = gh_tcp_get_u16_be(&pdu[6U + (i * 2U)]);
  }

  if (!GH_ModbusMap_WriteRange(address, qty, s_tcp.reg_scratch))
  {
    return gh_tcp_send_pdu(transaction_id,
                           unit_id,
                           gh_tcp_build_exception_pdu(GH_MB_FC_WRITE_MULTIPLE, GH_MB_EX_ILLEGAL_DATA_ADDRESS));
  }

  memcpy(&s_tcp.tx_buffer[GH_TCP_MBAP_HEADER_SIZE], pdu, 5U);
  return gh_tcp_send_pdu(transaction_id, unit_id, 5U);
}

static bool gh_tcp_process_request(uint16_t transaction_id,
                                   uint8_t unit_id,
                                   const uint8_t *pdu,
                                   uint16_t pdu_len)
{
  if ((pdu == NULL) || (pdu_len == 0U))
  {
    s_tcp.diag.malformedMbapCount++;
    gh_tcp_diag_note_recv_err((int32_t)ERR_VAL);
    gh_tcp_close_client();
    return false;
  }

  switch (pdu[0])
  {
    case GH_MB_FC_READ_HOLDING:
      return gh_tcp_handle_fc3(transaction_id, unit_id, pdu, pdu_len);

    case GH_MB_FC_WRITE_SINGLE:
      return gh_tcp_handle_fc6(transaction_id, unit_id, pdu, pdu_len);

    case GH_MB_FC_WRITE_MULTIPLE:
      return gh_tcp_handle_fc16(transaction_id, unit_id, pdu, pdu_len);

    default:
      return gh_tcp_send_pdu(transaction_id,
                             unit_id,
                             gh_tcp_build_exception_pdu(pdu[0], GH_MB_EX_ILLEGAL_FUNCTION));
  }
}

static bool gh_tcp_process_rx_buffer(void)
{
  uint16_t frame_len;
  uint16_t mbap_length;
  uint16_t transaction_id;
  uint16_t protocol_id;
  uint16_t remaining;
  uint16_t pdu_len;
  uint8_t unit_id;
  const uint8_t *pdu;

  while (s_tcp.rx_len >= GH_TCP_MBAP_HEADER_SIZE)
  {
    transaction_id = gh_tcp_get_u16_be(&s_tcp.rx_buffer[0]);
    protocol_id = gh_tcp_get_u16_be(&s_tcp.rx_buffer[2]);
    mbap_length = gh_tcp_get_u16_be(&s_tcp.rx_buffer[4]);

    if ((protocol_id != 0U) || (mbap_length < 2U) || (mbap_length > GH_TCP_MAX_MBAP_LENGTH))
    {
      s_tcp.diag.malformedMbapCount++;
      gh_tcp_diag_note_recv_err((int32_t)ERR_VAL);
      gh_tcp_close_client();
      return false;
    }

    frame_len = (uint16_t)(6U + mbap_length);
    if (frame_len > (uint16_t)sizeof(s_tcp.rx_buffer))
    {
      s_tcp.diag.malformedMbapCount++;
      gh_tcp_diag_note_recv_err((int32_t)ERR_BUF);
      gh_tcp_close_client();
      return false;
    }
    if (s_tcp.rx_len < frame_len)
    {
      return true;
    }

    unit_id = s_tcp.rx_buffer[6];
    pdu = &s_tcp.rx_buffer[7];
    pdu_len = (uint16_t)(mbap_length - 1U);
    if (!gh_tcp_process_request(transaction_id, unit_id, pdu, pdu_len))
    {
      return false;
    }

    remaining = (uint16_t)(s_tcp.rx_len - frame_len);
    if (remaining > 0U)
    {
      memmove(s_tcp.rx_buffer, &s_tcp.rx_buffer[frame_len], remaining);
    }
    s_tcp.rx_len = remaining;
    s_tcp.partial_frame_start_ms = (remaining > 0U) ? HAL_GetTick() : 0U;
  }

  return true;
}

static void gh_tcp_service_client(void)
{
  struct netbuf *inbuf = NULL;
  err_t recv_err;
  uint32_t now_ms;

  if (s_tcp.client == NULL)
  {
    return;
  }

  recv_err = netconn_recv(s_tcp.client, &inbuf);
  if (recv_err == ERR_TIMEOUT)
  {
    now_ms = HAL_GetTick();
    if ((s_tcp.rx_len > 0U) &&
        (s_tcp.partial_frame_start_ms != 0U) &&
        ((now_ms - s_tcp.partial_frame_start_ms) >= GH_TCP_PARTIAL_FRAME_TIMEOUT_MS))
    {
      s_tcp.diag.recvTimeoutCount++;
      gh_tcp_diag_note_recv_err((int32_t)ERR_TIMEOUT);
      gh_tcp_close_client();
      return;
    }

    if ((s_tcp.last_activity_ms != 0U) &&
        ((now_ms - s_tcp.last_activity_ms) >= GH_TCP_IDLE_TIMEOUT_MS))
    {
      s_tcp.diag.staleCloseCount++;
      gh_tcp_close_client();
    }
    return;
  }

  if (recv_err == ERR_CLSD)
  {
    s_tcp.diag.recvClosedCount++;
    gh_tcp_diag_note_recv_err((int32_t)recv_err);
    gh_tcp_close_client();
    return;
  }

  if (recv_err != ERR_OK)
  {
    s_tcp.diag.recvOtherErrCount++;
    gh_tcp_diag_note_recv_err((int32_t)recv_err);
    gh_tcp_close_client();
    return;
  }

  if (!gh_tcp_append_netbuf(inbuf))
  {
    s_tcp.diag.malformedMbapCount++;
    gh_tcp_diag_note_recv_err((int32_t)ERR_BUF);
    netbuf_delete(inbuf);
    gh_tcp_close_client();
    return;
  }

  netbuf_delete(inbuf);
  (void)gh_tcp_process_rx_buffer();
}

void GH_ModbusTcpServerTask_Run(void *argument)
{
  err_t accept_err;

  (void)argument;
  s_tcp.task_handle = xTaskGetCurrentTaskHandle();

#if defined(GH_USE_LWIP_NETCONN)
  for (;;)
  {
    task_heartbeat_kick(TASK_BIT_TCP);

    if (!gh_tcp_network_ready())
    {
      gh_tcp_note_link_down();
      osDelay(GH_TCP_NETIF_WAIT_STEP_MS);
      continue;
    }

    gh_tcp_note_link_up();

    if (!gh_tcp_ensure_listener())
    {
      osDelay(GH_TCP_NETIF_WAIT_STEP_MS);
      continue;
    }

    if (s_tcp.client == NULL)
    {
      accept_err = netconn_accept(s_tcp.listener, &s_tcp.client);
      if (accept_err == ERR_TIMEOUT)
      {
        continue;
      }
      if (accept_err != ERR_OK)
      {
        s_tcp.diag.acceptErrCount++;
        gh_tcp_diag_note_recv_err((int32_t)accept_err);
        gh_tcp_close_listener();
        osDelay(GH_TCP_NETIF_WAIT_STEP_MS);
        continue;
      }

      g_status.tcp_connect_count++;
      gh_tcp_reset_rx_state();
      s_tcp.last_activity_ms = HAL_GetTick();
      netconn_set_recvtimeout(s_tcp.client, GH_TCP_CLIENT_TIMEOUT_MS);
      continue;
    }

    gh_tcp_service_client();
  }
#else
  for (;;)
  {
    task_heartbeat_kick(TASK_BIT_TCP);
    osDelay(1000U);
  }
#endif
}

uint16_t GH_ModbusTcpServer_GetWorkerStackHwmWords(void)
{
  UBaseType_t words;

  if (s_tcp.task_handle == NULL)
  {
    return 0U;
  }

  words = uxTaskGetStackHighWaterMark(s_tcp.task_handle);
  if (words > 0xFFFFU)
  {
    words = 0xFFFFU;
  }
  return (uint16_t)words;
}

void GH_ModbusTcpServer_GetDiag(gh_modbus_tcp_diag_t *diagOut)
{
  if (diagOut == NULL)
  {
    return;
  }

  taskENTER_CRITICAL();
  *diagOut = s_tcp.diag;
  taskEXIT_CRITICAL();
}

void GH_ModbusTcpServer_ClearDiag(void)
{
  taskENTER_CRITICAL();
  memset(&s_tcp.diag, 0, sizeof(s_tcp.diag));
  taskEXIT_CRITICAL();
}
