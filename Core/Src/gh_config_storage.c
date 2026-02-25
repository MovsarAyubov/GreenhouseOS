#include "gh_config_storage.h"

#include "gh_runtime_state.h"
#include "gh_crc32.h"
#include "gh_modbus_map.h"
#include "gh_topology_v2.h"

#include <math.h>
#include <string.h>

typedef struct
{
  uint32_t start;
  uint32_t end;
} topo_range_t;

static void topo_runtime_clear(void)
{
  g_topology_v2_active = 0U;
  g_topology_v2_ver_major = 0U;
  g_topology_v2_ver_minor = 0U;
  g_topology_v2_generation = 0U;
  g_topology_v2_module_count = 0U;
  g_topology_v2_req_count = 0U;
  g_topology_v2_point_count = 0U;
  g_topology_v2_cmd_count = 0U;
  g_topology_v2_policy_count = 0U;
}

static bool topo_safe_mul_u32(uint32_t a, uint32_t b, uint32_t *out)
{
  if (out == NULL)
  {
    return false;
  }
  if ((a != 0U) && (b > (0xFFFFFFFFUL / a)))
  {
    return false;
  }
  *out = a * b;
  return true;
}

static bool topo_section_bounds(uint32_t off,
                                uint32_t count,
                                uint32_t elem_size,
                                uint32_t total_size,
                                topo_range_t *out_range)
{
  uint32_t bytes;

  if (out_range == NULL)
  {
    return false;
  }

  out_range->start = 0U;
  out_range->end = 0U;

  if (count == 0U)
  {
    return true;
  }

  if ((off < sizeof(gh_topology_v2_header_t)) || (off >= total_size) || ((off & 0x1U) != 0U))
  {
    return false;
  }

  if (!topo_safe_mul_u32(count, elem_size, &bytes) || (bytes == 0U))
  {
    return false;
  }
  if (off > (total_size - bytes))
  {
    return false;
  }

  out_range->start = off;
  out_range->end = off + bytes;
  return true;
}

static bool topo_ranges_overlap(const topo_range_t *a, const topo_range_t *b)
{
  if ((a == NULL) || (b == NULL))
  {
    return false;
  }
  if ((a->end <= a->start) || (b->end <= b->start))
  {
    return false;
  }
  return !((a->end <= b->start) || (b->end <= a->start));
}

bool GH_TopologyV2_IsPayload(const uint8_t *payload, uint32_t payload_len)
{
  uint32_t magic;

  if ((payload == NULL) || (payload_len < sizeof(uint32_t)))
  {
    return false;
  }

  memcpy(&magic, payload, sizeof(magic));
  return (magic == GH_TOPOLOGY_V2_MAGIC);
}

bool GH_TopologyV2_ValidatePayload(const uint8_t *payload,
                                   uint32_t payload_len,
                                   config_result_code_t *out_result)
{
  gh_topology_v2_header_t hdr;
  gh_topology_v2_header_t hdr_crc_copy;
  topo_range_t ranges[5];
  uint8_t i;
  uint8_t j;
  uint32_t body_crc;
  uint32_t header_crc;

  if (out_result == NULL)
  {
    return false;
  }
  *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;

  if ((payload == NULL) || (payload_len < sizeof(hdr)))
  {
    return false;
  }

  memcpy(&hdr, payload, sizeof(hdr));
  if (hdr.magic != GH_TOPOLOGY_V2_MAGIC)
  {
    return false;
  }
  if (hdr.ver_major != GH_TOPOLOGY_V2_VERSION_MAJOR)
  {
    return false;
  }
  if ((hdr.total_size < sizeof(hdr)) || (hdr.total_size > payload_len))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
    return false;
  }

  memcpy(&hdr_crc_copy, &hdr, sizeof(hdr_crc_copy));
  hdr_crc_copy.header_crc32 = 0U;
  header_crc = gh_crc32_compute((const uint8_t *)&hdr_crc_copy, sizeof(hdr_crc_copy));
  body_crc = gh_crc32_compute(&payload[sizeof(hdr)], hdr.total_size - sizeof(hdr));
  if ((header_crc != hdr.header_crc32) || (body_crc != hdr.body_crc32))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_CRC;
    return false;
  }

  if ((hdr.module_count > GH_TOPOLOGY_V2_MAX_MODULES) ||
      (hdr.req_count > GH_TOPOLOGY_V2_MAX_REQ_PROFILES) ||
      (hdr.point_count > GH_TOPOLOGY_V2_MAX_POINTS) ||
      (hdr.cmd_count > GH_TOPOLOGY_V2_MAX_COMMANDS) ||
      (hdr.policy_count > GH_TOPOLOGY_V2_MAX_POLICIES))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_BUDGET;
    return false;
  }

  if (!topo_section_bounds(hdr.off_modules,
                           hdr.module_count,
                           sizeof(gh_topology_v2_module_t),
                           hdr.total_size,
                           &ranges[0]) ||
      !topo_section_bounds(hdr.off_requests,
                           hdr.req_count,
                           sizeof(gh_topology_v2_req_t),
                           hdr.total_size,
                           &ranges[1]) ||
      !topo_section_bounds(hdr.off_points,
                           hdr.point_count,
                           sizeof(gh_topology_v2_point_t),
                           hdr.total_size,
                           &ranges[2]) ||
      !topo_section_bounds(hdr.off_commands,
                           hdr.cmd_count,
                           sizeof(gh_topology_v2_cmd_t),
                           hdr.total_size,
                           &ranges[3]) ||
      !topo_section_bounds(hdr.off_policies,
                           hdr.policy_count,
                           sizeof(gh_topology_v2_policy_t),
                           hdr.total_size,
                           &ranges[4]))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
    return false;
  }

  for (i = 0U; i < 5U; i++)
  {
    for (j = (uint8_t)(i + 1U); j < 5U; j++)
    {
      if (topo_ranges_overlap(&ranges[i], &ranges[j]))
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
    }
  }

  *out_result = CFG_RESULT_IDLE;
  return true;
}

void GH_TopologyV2_SyncRuntimeFromConfig(const active_config_t *cfg)
{
  gh_topology_v2_header_t hdr;
  config_result_code_t result = CFG_RESULT_IDLE;

  topo_runtime_clear();

  if (cfg == NULL)
  {
    return;
  }
  if (!GH_TopologyV2_IsPayload(cfg->payload, CONFIG_PAYLOAD_SIZE))
  {
    return;
  }
  if (!GH_TopologyV2_ValidatePayload(cfg->payload, CONFIG_PAYLOAD_SIZE, &result))
  {
    return;
  }

  memcpy(&hdr, cfg->payload, sizeof(hdr));
  g_topology_v2_active = 1U;
  g_topology_v2_ver_major = hdr.ver_major;
  g_topology_v2_ver_minor = hdr.ver_minor;
  g_topology_v2_generation = hdr.generation;
  g_topology_v2_module_count = hdr.module_count;
  g_topology_v2_req_count = hdr.req_count;
  g_topology_v2_point_count = hdr.point_count;
  g_topology_v2_cmd_count = hdr.cmd_count;
  g_topology_v2_policy_count = hdr.policy_count;
}

bool GH_ConfigStorage_PayloadValuesValid(const uint8_t *payload)
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

bool GH_ConfigStorage_ValidateRequest(const config_update_req_t *req,
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

  if (GH_TopologyV2_IsPayload(req->payload, CONFIG_PAYLOAD_SIZE))
  {
    return GH_TopologyV2_ValidatePayload(req->payload, CONFIG_PAYLOAD_SIZE, out_result);
  }

  if (!GH_ConfigStorage_PayloadValuesValid(req->payload))
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

      if (!GH_ConfigStorage_ValidateRequest(&req, &validation_result))
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
