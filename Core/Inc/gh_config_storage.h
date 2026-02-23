#ifndef GH_CONFIG_STORAGE_H
#define GH_CONFIG_STORAGE_H

#include "gh_runtime_state.h"

#include <stdbool.h>

void GH_ConfigStorageTask_Run(void *argument);
bool GH_ConfigStorage_PayloadValuesValid(const uint8_t *payload);
bool GH_ConfigStorage_ValidateRequest(const config_update_req_t *req,
                                      config_result_code_t *out_result);

#endif /* GH_CONFIG_STORAGE_H */
