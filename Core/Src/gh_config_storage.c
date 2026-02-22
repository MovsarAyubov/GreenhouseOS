#include "gh_config_storage.h"

#include "gh_runtime_state.h"

#include <string.h>

void GH_ConfigStorageTask_Run(void *argument)
{
  config_update_req_t req = {0};
  active_config_t pending = {0};
  bool use_slot_a = true;
  bool ok;
  (void)argument;

  for (;;)
  {
    if (osMessageQueueGet(qConfigStoreHandle, &req, NULL, 100U) == osOK)
    {
      pending.version = req.version;
      pending.crc = req.payload_crc;
      memcpy(pending.payload, req.payload, CONFIG_PAYLOAD_SIZE);

      ok = use_slot_a ?
           config_write_to_slot(CONFIG_SLOT_A_ADDR, CONFIG_SLOT_A_SECTOR, &pending) :
           config_write_to_slot(CONFIG_SLOT_B_ADDR, CONFIG_SLOT_B_SECTOR, &pending);
      use_slot_a = !use_slot_a;

      if (ok)
      {
        g_status.flash_write_ok_count++;
        (void)osMessageQueuePut(qConfigApplyHandle, &pending, 0U, osWaitForever);
      }
      else
      {
        g_status.flash_write_fail_count++;
      }
      g_setpoints_apply_in_progress = false;
    }

    task_heartbeat_kick(TASK_BIT_CONFIG);
    osDelay(20U);
  }
}
