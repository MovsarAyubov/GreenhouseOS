#include "ut_os_hooks.h"

#include <string.h>

uint32_t ut_queue_put_count = 0U;
uint32_t ut_queue_get_count = 0U;
osStatus_t ut_queue_put_status = osOK;
osStatus_t ut_queue_get_status = osError;
config_update_req_t ut_last_queue_req = {0};

static osKernelState_t s_kernel_state = osKernelRunning;

void UT_OsHooks_Reset(void)
{
  ut_queue_put_count = 0U;
  ut_queue_get_count = 0U;
  ut_queue_put_status = osOK;
  ut_queue_get_status = osError;
  memset(&ut_last_queue_req, 0, sizeof(ut_last_queue_req));
  s_kernel_state = osKernelRunning;
}

osKernelState_t osKernelGetState(void)
{
  return s_kernel_state;
}

osMutexId_t osMutexNew(const void *attr)
{
  (void)attr;
  return (osMutexId_t)0x1;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
  (void)mutex_id;
  (void)timeout;
  return osOK;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
  (void)mutex_id;
  return osOK;
}

osStatus_t osMutexDelete(osMutexId_t mutex_id)
{
  (void)mutex_id;
  return osOK;
}

osEventFlagsId_t osEventFlagsNew(const void *attr)
{
  (void)attr;
  return (osEventFlagsId_t)0x1;
}

uint32_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags)
{
  (void)ef_id;
  return flags;
}

uint32_t osEventFlagsClear(osEventFlagsId_t ef_id, uint32_t flags)
{
  (void)ef_id;
  return flags;
}

uint32_t osEventFlagsWait(osEventFlagsId_t ef_id, uint32_t flags, uint32_t options, uint32_t timeout)
{
  (void)ef_id;
  (void)options;
  (void)timeout;
  return flags;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id,
                             const void *msg_ptr,
                             uint8_t msg_prio,
                             uint32_t timeout)
{
  (void)mq_id;
  (void)msg_prio;
  (void)timeout;

  ut_queue_put_count++;
  if (ut_queue_put_status != osOK)
  {
    return ut_queue_put_status;
  }

  if (msg_ptr != NULL)
  {
    memcpy(&ut_last_queue_req, msg_ptr, sizeof(ut_last_queue_req));
  }

  return osOK;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id,
                             void *msg_ptr,
                             uint8_t *msg_prio,
                             uint32_t timeout)
{
  (void)mq_id;
  (void)msg_ptr;
  (void)msg_prio;
  (void)timeout;
  ut_queue_get_count++;
  return ut_queue_get_status;
}

void osDelay(uint32_t ticks)
{
  (void)ticks;
}
