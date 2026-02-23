#ifndef UT_OS_HOOKS_H
#define UT_OS_HOOKS_H

#include "gh_runtime_state.h"

#include <stdint.h>

extern uint32_t ut_queue_put_count;
extern uint32_t ut_queue_get_count;
extern osStatus_t ut_queue_put_status;
extern osStatus_t ut_queue_get_status;
extern config_update_req_t ut_last_queue_req;

void UT_OsHooks_Reset(void);

#endif /* UT_OS_HOOKS_H */
