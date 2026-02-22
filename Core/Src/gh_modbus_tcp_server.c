#include "gh_modbus_tcp_server.h"

#include "gh_modbus_map.h"
#include "Modbus.h"
#include "cmsis_os.h"
#include "lwip.h"

static modbusHandler_t s_mb_tcp = {0};
static bool s_mb_started = false;
extern struct netif gnetif;

void GH_ModbusTcpServerTask_Run(void *argument)
{
  uint16_t *regs;
  (void)argument;

#if defined(GH_USE_LWIP_NETCONN)
  if (!s_mb_started)
  {
    while (gnetif.input == NULL)
    {
      osDelay(50U);
    }

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
    s_mb_started = true;
  }
#endif

  for (;;)
  {
    osDelay(1000U);
  }
}
