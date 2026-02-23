#ifndef CMSIS_OS_H
#define CMSIS_OS_H

#include <stdint.h>

typedef void *osMutexId_t;
typedef void *osEventFlagsId_t;
typedef void *osMessageQueueId_t;

typedef enum
{
  osOK = 0,
  osError = 1,
  osErrorTimeout = 2
} osStatus_t;

typedef enum
{
  osKernelInactive = 0,
  osKernelReady = 1,
  osKernelRunning = 2
} osKernelState_t;

#define osWaitForever 0xFFFFFFFFUL
#define osFlagsError 0x80000000UL

osKernelState_t osKernelGetState(void);

osMutexId_t osMutexNew(const void *attr);
osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout);
osStatus_t osMutexRelease(osMutexId_t mutex_id);
osStatus_t osMutexDelete(osMutexId_t mutex_id);

osEventFlagsId_t osEventFlagsNew(const void *attr);
uint32_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags);
uint32_t osEventFlagsClear(osEventFlagsId_t ef_id, uint32_t flags);
uint32_t osEventFlagsWait(osEventFlagsId_t ef_id, uint32_t flags, uint32_t options, uint32_t timeout);

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id,
                             const void *msg_ptr,
                             uint8_t msg_prio,
                             uint32_t timeout);
osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id,
                             void *msg_ptr,
                             uint8_t *msg_prio,
                             uint32_t timeout);

void osDelay(uint32_t ticks);

#endif /* CMSIS_OS_H */
