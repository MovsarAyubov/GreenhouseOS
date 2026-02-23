#include "gh_config_storage.h"

#include "gh_runtime_state.h"
#include "gh_crc32.h"
#include "gh_modbus_map.h"

#include <math.h>
#include <string.h>

static bool cfg_payload_values_valid(const uint8_t *payload)
{
  uint16_t i;

  for (i = 0U; i < (CONFIG_PAYLOAD_SIZE / sizeof(float)); i++)
  {
    float v;
    memcpy(&v, &payload[i * sizeof(float)], sizeof(float));
    if (!isfinite(v) || (v < -100.0f) || (v > 1000.0f))
    {
      return false;
    }
  }

  return true;
}

static bool cfg_validate_request(const config_update_req_t *req,
                                 config_result_code_t *out_result)
{
  uint32_t crc;

  if ((req == NULL) || (out_result == NULL))
  {
    return false;
  }

  if (req->version <= g_active_config.version)
  {
    *out_result = CFG_RESULT_REJECT_BAD_VERSION;
    return false;
  }

  crc = gh_crc32_compute(req->payload, CONFIG_PAYLOAD_SIZE);
  if (crc != req->payload_crc)
  {
    *out_result = CFG_RESULT_REJECT_BAD_CRC;
    return false;
  }

  if (!cfg_payload_values_valid(req->payload))
  {
    *out_result = CFG_RESULT_REJECT_RANGE;
    return false;
  }

  *out_result = CFG_RESULT_IDLE;
  return true;
}

static bool cfg_write_slot_with_retries(uint32_t slot_addr,
                                        uint32_t sector,
                                        const active_config_t *cfg)
{
  uint8_t attempt;

  for (attempt = 0U; attempt < CONFIG_FLASH_WRITE_RETRIES; attempt++)
  {
    if (config_write_to_slot(slot_addr, sector, cfg))
    {
      return true;
    }
    if ((attempt + 1U) < CONFIG_FLASH_WRITE_RETRIES)
    {
      osDelay(CONFIG_FLASH_RETRY_DELAY_MS);
    }
  }

  return false;
}

static bool cfg_store_ab_with_fallback(bool prefer_slot_a,
                                       const active_config_t *cfg,
                                       bool *out_slot_a_used)
{
  bool ok;

  if ((cfg == NULL) || (out_slot_a_used == NULL))
  {
    return false;
  }

  if (prefer_slot_a)
  {
    ok = cfg_write_slot_with_retries(CONFIG_SLOT_A_ADDR, CONFIG_SLOT_A_SECTOR, cfg);
    if (ok)
    {
      *out_slot_a_used = true;
      return true;
    }
    ok = cfg_write_slot_with_retries(CONFIG_SLOT_B_ADDR, CONFIG_SLOT_B_SECTOR, cfg);
    if (ok)
    {
      *out_slot_a_used = false;
      return true;
    }
  }
  else
  {
    ok = cfg_write_slot_with_retries(CONFIG_SLOT_B_ADDR, CONFIG_SLOT_B_SECTOR, cfg);
    if (ok)
    {
      *out_slot_a_used = false;
      return true;
    }
    ok = cfg_write_slot_with_retries(CONFIG_SLOT_A_ADDR, CONFIG_SLOT_A_SECTOR, cfg);
    if (ok)
    {
      *out_slot_a_used = true;
      return true;
    }
  }

  return false;
}

void GH_ConfigStorageTask_Run(void *argument)
{
  config_update_req_t req = {0};
  config_apply_req_t apply_req = {0};
  active_config_t pending = {0};
  bool prefer_slot_a = true;
  bool written_slot_a = false;
  bool ok;
  uint8_t attempt;
  config_result_code_t validation_result = CFG_RESULT_IDLE;
  (void)argument;

  for (;;)
  {
    if (osMessageQueueGet(qConfigStoreHandle, &req, NULL, 100U) == osOK)
    {
      pending.version = req.version;
      pending.crc = req.payload_crc;
      memcpy(pending.payload, req.payload, CONFIG_PAYLOAD_SIZE);

      if (!cfg_validate_request(&req, &validation_result))
      {
        GH_ModbusMap_ReportConfigResult(req.request_token, validation_result, g_active_config.version);
        publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)validation_result);
        g_setpoints_apply_in_progress = false;
        continue;
      }

      ok = cfg_store_ab_with_fallback(prefer_slot_a, &pending, &written_slot_a);

      if (ok)
      {
        g_status.flash_write_ok_count++;
        prefer_slot_a = !written_slot_a;

        apply_req.request_token = req.request_token;
        apply_req.reserved0 = 0U;
        apply_req.config = pending;

        ok = false;
        for (attempt = 0U; attempt < CONFIG_APPLY_QUEUE_RETRIES; attempt++)
        {
          if (osMessageQueuePut(qConfigApplyHandle, &apply_req, 0U, 0U) == osOK)
          {
            ok = true;
            break;
          }
          if ((attempt + 1U) < CONFIG_APPLY_QUEUE_RETRIES)
          {
            osDelay(CONFIG_APPLY_QUEUE_DELAY_MS);
          }
        }

        if (!ok)
        {
          GH_ModbusMap_ReportConfigResult(req.request_token,
                                          CFG_RESULT_APPLY_QUEUE_FAIL,
                                          g_active_config.version);
          publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_APPLY_QUEUE_FAIL);
          g_setpoints_apply_in_progress = false;
        }
      }
      else
      {
        g_status.flash_write_fail_count++;
        GH_ModbusMap_ReportConfigResult(req.request_token, CFG_RESULT_FLASH_FAIL, g_active_config.version);
        publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_FLASH_FAIL);
        g_setpoints_apply_in_progress = false;
      }
    }

    task_heartbeat_kick(TASK_BIT_CONFIG);
    osDelay(20U);
  }
}
