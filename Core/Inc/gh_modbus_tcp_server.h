#ifndef GH_MODBUS_TCP_SERVER_H
#define GH_MODBUS_TCP_SERVER_H

#include <stdint.h>

typedef struct
{
  uint32_t acceptErrCount;
  uint32_t recvTimeoutCount;
  uint32_t recvClosedCount;
  uint32_t recvOtherErrCount;
  uint32_t staleCloseCount;
  uint32_t malformedMbapCount;
  uint32_t sendErrCount;
  int32_t lastRecvErr;
  int32_t lastSendErr;
} gh_modbus_tcp_diag_t;

void GH_ModbusTcpServerTask_Run(void *argument);
uint16_t GH_ModbusTcpServer_GetWorkerStackHwmWords(void);
void GH_ModbusTcpServer_GetDiag(gh_modbus_tcp_diag_t *diagOut);
void GH_ModbusTcpServer_ClearDiag(void);

#endif /* GH_MODBUS_TCP_SERVER_H */
