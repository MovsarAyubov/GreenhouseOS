#ifndef GH_NET_ADAPTER_H
#define GH_NET_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

void NetAdapter_Init(uint16_t port);
void NetAdapter_ServerPoll(void);
bool NetAdapter_IsConnected(void);
int32_t NetAdapter_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);
bool NetAdapter_Send(const uint8_t *data, uint16_t len);
void NetAdapter_CloseClient(void);

#endif /* GH_NET_ADAPTER_H */
