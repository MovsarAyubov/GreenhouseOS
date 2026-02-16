#include "gh_net_adapter.h"
#include <string.h>

#if defined(GH_USE_LWIP_NETCONN)
#include "lwip/api.h"
#include "lwip/err.h"
#endif

static uint16_t s_port = 0U;
static bool s_connected = false;
static bool s_initialized = false;

#if defined(GH_USE_LWIP_NETCONN)
static struct netconn *s_server = NULL;
static struct netconn *s_client = NULL;
static struct netbuf *s_rx_buf = NULL;
static uint16_t s_rx_off = 0U;

void NetAdapter_Init(uint16_t port)
{
  s_port = port;
  s_initialized = false;
  s_server = netconn_new(NETCONN_TCP);
  if (s_server == NULL)
  {
    return;
  }
  if (netconn_bind(s_server, IP_ADDR_ANY, s_port) != ERR_OK)
  {
    netconn_delete(s_server);
    s_server = NULL;
    return;
  }
  (void)netconn_listen(s_server);
  netconn_set_nonblocking(s_server, 1);
  s_initialized = true;
}

void NetAdapter_ServerPoll(void)
{
  struct netconn *new_client = NULL;
  if (s_server == NULL)
  {
    return;
  }

  if (netconn_accept(s_server, &new_client) == ERR_OK)
  {
    if (s_client != NULL)
    {
      netconn_close(s_client);
      netconn_delete(s_client);
      s_client = NULL;
    }
    s_client = new_client;
    s_connected = true;
    netconn_set_nonblocking(s_client, 1);
  }
}

bool NetAdapter_IsConnected(void)
{
  return s_initialized && s_connected && (s_client != NULL);
}

int32_t NetAdapter_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
  err_t err;
  void *data;
  uint16_t len;
  uint16_t copy_len;

  if ((s_client == NULL) || (buf == NULL) || (max_len == 0U))
  {
    return 0;
  }

  (void)timeout_ms;

  if (s_rx_buf == NULL)
  {
    err = netconn_recv(s_client, &s_rx_buf);
    if (err != ERR_OK)
    {
      if ((err == ERR_TIMEOUT) || (err == ERR_WOULDBLOCK))
      {
        return 0;
      }
      NetAdapter_CloseClient();
      return -1;
    }
    s_rx_off = 0U;
  }

  netbuf_data(s_rx_buf, &data, &len);
  if (s_rx_off >= len)
  {
    netbuf_delete(s_rx_buf);
    s_rx_buf = NULL;
    s_rx_off = 0U;
    return 0;
  }

  copy_len = (uint16_t)(len - s_rx_off);
  if (copy_len > max_len)
  {
    copy_len = max_len;
  }
  memcpy(buf, (const uint8_t *)data + s_rx_off, copy_len);
  s_rx_off = (uint16_t)(s_rx_off + copy_len);

  if (s_rx_off >= len)
  {
    netbuf_delete(s_rx_buf);
    s_rx_buf = NULL;
    s_rx_off = 0U;
  }

  return (int32_t)copy_len;
}

bool NetAdapter_Send(const uint8_t *data, uint16_t len)
{
  if ((s_client == NULL) || (data == NULL) || (len == 0U))
  {
    return false;
  }
  if (netconn_write(s_client, data, len, NETCONN_COPY) != ERR_OK)
  {
    NetAdapter_CloseClient();
    return false;
  }
  return true;
}

void NetAdapter_CloseClient(void)
{
  if (s_rx_buf != NULL)
  {
    netbuf_delete(s_rx_buf);
    s_rx_buf = NULL;
  }
  if (s_client != NULL)
  {
    netconn_close(s_client);
    netconn_delete(s_client);
    s_client = NULL;
  }
  s_connected = false;
  s_initialized = (s_server != NULL);
}

#else

void NetAdapter_Init(uint16_t port)
{
  s_port = port;
  (void)s_port;
  s_initialized = true;
}

void NetAdapter_ServerPoll(void)
{
}

bool NetAdapter_IsConnected(void)
{
  return false;
}

int32_t NetAdapter_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
  (void)buf;
  (void)max_len;
  (void)timeout_ms;
  return 0;
}

bool NetAdapter_Send(const uint8_t *data, uint16_t len)
{
  (void)data;
  (void)len;
  return false;
}

void NetAdapter_CloseClient(void)
{
}

#endif
