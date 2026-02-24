#include "gh_modbus_tcp_server.h"

#include "gh_runtime_state.h"
#include "gh_modbus_map.h"
#include "Modbus.h"
#include "cmsis_os.h"
#include "lwip.h"
#include "lwip/netif.h"

#define GH_TCP_NETIF_WAIT_STEP_MS 100U
#define GH_TCP_NETIF_WAIT_TIMEOUT_MS 10000U

static modbusHandler_t s_mb_tcp = {0};
static bool s_mb_started = false;
static bool s_mb_init_attempted = false;
static uint32_t s_netif_wait_start_ms = 0U;
static bool s_link_down_reported = false;
extern struct netif gnetif;

void GH_ModbusTcpServerTask_Run(void *argument)
{
  (void)argument;

#if defined(GH_USE_LWIP_NETCONN)
  uint16_t *regs;
  for (;;)
  {
    if (!netif_is_up(&gnetif) || !netif_is_link_up(&gnetif))
    {
      s_mb_started = false;
      s_mb_init_attempted = false;
      task_heartbeat_kick(TASK_BIT_TCP);
      osDelay(GH_TCP_NETIF_WAIT_STEP_MS);
      continue;
    }

    if (!s_mb_started)
    {
      if (!s_mb_init_attempted)
      {
        regs = GH_ModbusMap_GetBackingStore();
        s_mb_tcp.uModbusType = MB_SLAVE;
        s_mb_tcp.xTypeHW = TCP_HW;
        s_mb_tcp.u8id = 1U;
        s_mb_tcp.u16regs = regs;
        s_mb_tcp.u16regsize = GH_ModbusMap_GetBackingStoreSize();
#ifdef TCP_SERVER_RECV_TIMEOUT_MS
        s_mb_tcp.u16timeOut = (uint16_t)TCP_SERVER_RECV_TIMEOUT_MS;
#else
        s_mb_tcp.u16timeOut = 5000U;
#endif
        s_mb_tcp.uTcpPort = 502U;
        s_mb_tcp.port = NULL;
        s_mb_tcp.EN_Port = NULL;
        s_mb_tcp.EN_Pin = 0U;

        ModbusInit(&s_mb_tcp);
        ModbusStart(&s_mb_tcp);
        s_mb_init_attempted = true;
      }

      if (s_mb_tcp.myTaskModbusAHandle != NULL)
      {
        s_mb_started = true;
        s_netif_wait_start_ms = 0U;
        if (s_link_down_reported)
        {
          s_link_down_reported = false;
          if (g_status.last_error_code == EVENT_CODE_LINK_DOWN)
          {
            g_status.last_error_code = 0U;
          }
          publish_event(EVENT_SEV_INFO, EVENT_CODE_LINK_UP, 0U, 0.0f);
        }
      }
      else
      {
        s_mb_init_attempted = false;
        if (s_netif_wait_start_ms == 0U)
        {
          s_netif_wait_start_ms = HAL_GetTick();
        }
        else if (!s_link_down_reported &&
                 ((HAL_GetTick() - s_netif_wait_start_ms) >= GH_TCP_NETIF_WAIT_TIMEOUT_MS))
        {
          s_link_down_reported = true;
          g_status.last_error_code = EVENT_CODE_LINK_DOWN;
          publish_event(EVENT_SEV_WARN, EVENT_CODE_LINK_DOWN, 0U, 0.0f);
        }
      }
    }
    task_heartbeat_kick(TASK_BIT_TCP);
    if (!s_mb_started)
    {
      osDelay(GH_TCP_NETIF_WAIT_STEP_MS);
    }
    else
    {
      osDelay(1000U);
    }
  }
#else
  for (;;)
  {
    task_heartbeat_kick(TASK_BIT_TCP);
    osDelay(1000U);
  }
#endif
}
