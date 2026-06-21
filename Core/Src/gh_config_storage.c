#include "gh_config_storage.h"

#include "gh_runtime_state.h"
#include "gh_crc32.h"
#include "gh_modbus_map.h"
#include "gh_topology_v2.h"
#include "gh_topology_runtime.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#if !defined(FLASH_TYPEPROGRAM_WORD)
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define FLASH_TYPEPROGRAM_WORD 0U
static void HAL_FLASH_Unlock(void) {}
static void HAL_FLASH_Lock(void) {}
static HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uint32_t Address, uint64_t Data)
{
  (void)TypeProgram;
  (void)Address;
  (void)Data;
  return HAL_OK;
}
#endif

typedef struct
{
  uint32_t start;
  uint32_t end;
} topo_range_t;

typedef struct __attribute__((packed))
{
  uint32_t total_size;
  uint32_t generation;
  uint32_t blob_crc;
  uint32_t valid_marker;
} topology_slot_header_t;

static uint8_t s_topology_active_blob[TOPOLOGY_MAX_BLOB_SIZE];
static uint32_t s_topology_active_size = 0U;
static uint32_t s_topology_active_generation = 0U;
static bool s_topology_active_valid = false;
static bool s_topology_prefer_slot_a = true;

static uint8_t s_topology_staging_blob[TOPOLOGY_MAX_BLOB_SIZE];
static uint32_t s_topology_staging_size = 0U;
static uint32_t s_topology_staging_generation = 0U;
static uint32_t s_topology_staging_chunks_mask = 0U;

#define GH_TOPOLOGY_RUNTIME_MAX_REQS        GH_TOPOLOGY_V2_MAX_REQ_PROFILES
#define GH_TOPOLOGY_RUNTIME_MAX_POINTS      GH_TOPOLOGY_V2_MAX_POINTS
#define GH_TOPOLOGY_RUNTIME_MAX_CMDS        GH_TOPOLOGY_V2_MAX_COMMANDS
#define GH_TOPOLOGY_RUNTIME_MAX_POLICIES    GH_TOPOLOGY_V2_MAX_POLICIES
#define GH_TOPOLOGY_RUNTIME_BUS_RTU1        1U
#define GH_TOPOLOGY_RUNTIME_FC_READ_HOLDING 3U
#define GH_TOPOLOGY_RUNTIME_DIAG_BASE_REG   9U
#define GH_TOPOLOGY_RUNTIME_DIAG_REG_COUNT  6U
#define GH_TOPOLOGY_RUNTIME_SENSOR_WORDS    9U
#define GH_TOPOLOGY_RUNTIME_CMD_WORDS       TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS
#define GH_TOPOLOGY_RUNTIME_SCHED_SLOT_WORDS 3U
#define GH_TOPOLOGY_RUNTIME_SCHED_SLOT_COUNT 4U
#define GH_TOPOLOGY_RUNTIME_SCHED_WORDS      (GH_TOPOLOGY_RUNTIME_SCHED_SLOT_WORDS * GH_TOPOLOGY_RUNTIME_SCHED_SLOT_COUNT)
#define GH_TOPOLOGY_POLICY_ACTION_MAX       GH_TOPOLOGY_POLICY_ACTION_FORCE_OFFLINE

#define GH_TOPOLOGY_BUS_RTU1                1U
#define GH_TOPOLOGY_BUS_RTU2                2U
#define GH_TOPOLOGY_BUS_TCP                 3U

#define GH_TOPOLOGY_REQ_FC_READ_HOLDING     3U
#define GH_TOPOLOGY_REQ_FC_READ_INPUT       4U

#define GH_TOPOLOGY_CMD_FC_WRITE_SINGLE     6U
#define GH_TOPOLOGY_CMD_FC_WRITE_MULTI      16U

#define GH_TOPOLOGY_POINT_TYPE_U16          1U
#define GH_TOPOLOGY_POINT_TYPE_S16          2U
#define GH_TOPOLOGY_POINT_TYPE_U32          3U
#define GH_TOPOLOGY_POINT_TYPE_S32          4U
#define GH_TOPOLOGY_POINT_TYPE_FLOAT        5U
#define GH_TOPOLOGY_POINT_TYPE_BIT          6U

#define GH_TOPOLOGY_RTU_SLAVE_MAX           247U

static gh_topology_poll_req_t s_poll_reqs[GH_TOPOLOGY_RUNTIME_MAX_REQS] __attribute__((section(".ccmram")));
static gh_topology_point_binding_t s_point_bindings[GH_TOPOLOGY_RUNTIME_MAX_POINTS] __attribute__((section(".ccmram")));
static gh_topology_cmd_binding_t s_cmd_bindings[GH_TOPOLOGY_RUNTIME_MAX_CMDS] __attribute__((section(".ccmram")));
static gh_topology_policy_binding_t s_policy_bindings[GH_TOPOLOGY_RUNTIME_MAX_POLICIES] __attribute__((section(".ccmram")));
static uint16_t s_poll_req_count = 0U;
static uint16_t s_point_binding_count = 0U;
static uint16_t s_cmd_binding_count = 0U;
static uint16_t s_policy_binding_count = 0U;
static uint32_t s_poll_generation = 0U;
static uint32_t s_rtu1_slave_mask = 0U;
static osMutexId_t s_runtime_mutex = NULL;

static bool runtime_ensure_mutex(void)
{
  if (s_runtime_mutex != NULL)
  {
    return true;
  }

  if (osKernelGetState() != osKernelRunning)
  {
    return true;
  }

  s_runtime_mutex = osMutexNew(NULL);
  return (s_runtime_mutex != NULL);
}

static bool runtime_lock(void)
{
  if (!runtime_ensure_mutex())
  {
    return false;
  }

  if (s_runtime_mutex == NULL)
  {
    return true;
  }

  return (osMutexAcquire(s_runtime_mutex, osWaitForever) == osOK);
}

static void runtime_unlock(void)
{
  if (s_runtime_mutex != NULL)
  {
    (void)osMutexRelease(s_runtime_mutex);
  }
}

static uint8_t telemetry_word_count_for_req(const gh_topology_v2_req_t *req)
{
  if ((req == NULL) || (req->start_reg != 0U))
  {
    return 0U;
  }
  if (req->reg_count >= GH_TOPOLOGY_RUNTIME_SENSOR_WORDS)
  {
    return GH_TOPOLOGY_RUNTIME_SENSOR_WORDS;
  }
  return (uint8_t)req->reg_count;
}

static uint8_t diag_offset_for_req(const gh_topology_v2_req_t *req)
{
  uint32_t req_end;
  uint32_t diag_end;

  if (req == NULL)
  {
    return GH_TOPOLOGY_DIAG_OFFSET_NONE;
  }

  req_end = (uint32_t)req->start_reg + (uint32_t)req->reg_count;
  diag_end = GH_TOPOLOGY_RUNTIME_DIAG_BASE_REG + GH_TOPOLOGY_RUNTIME_DIAG_REG_COUNT;
  if (((uint32_t)req->start_reg > GH_TOPOLOGY_RUNTIME_DIAG_BASE_REG) || (req_end < diag_end))
  {
    return GH_TOPOLOGY_DIAG_OFFSET_NONE;
  }

  return (uint8_t)(GH_TOPOLOGY_RUNTIME_DIAG_BASE_REG - req->start_reg);
}

static uint8_t topo_point_word_width(uint8_t point_type)
{
  switch (point_type)
  {
    case GH_TOPOLOGY_POINT_TYPE_U16:
    case GH_TOPOLOGY_POINT_TYPE_S16:
    case GH_TOPOLOGY_POINT_TYPE_BIT:
      return 1U;
    case GH_TOPOLOGY_POINT_TYPE_U32:
    case GH_TOPOLOGY_POINT_TYPE_S32:
    case GH_TOPOLOGY_POINT_TYPE_FLOAT:
      return 2U;
    default:
      return 0U;
  }
}

void GH_TopologyRuntime_Clear(void)
{
  if (!runtime_lock())
  {
    return;
  }

  memset(s_poll_reqs, 0, sizeof(s_poll_reqs));
  memset(s_point_bindings, 0, sizeof(s_point_bindings));
  memset(s_cmd_bindings, 0, sizeof(s_cmd_bindings));
  memset(s_policy_bindings, 0, sizeof(s_policy_bindings));
  s_poll_req_count = 0U;
  s_point_binding_count = 0U;
  s_cmd_binding_count = 0U;
  s_policy_binding_count = 0U;
  s_poll_generation = 0U;
  s_rtu1_slave_mask = 0U;

  runtime_unlock();
}

bool GH_TopologyRuntime_RebuildFromPayload(const uint8_t *payload, uint32_t payload_len)
{
  gh_topology_v2_header_t hdr;
  gh_topology_v2_module_t mod;
  gh_topology_v2_req_t req;
  gh_topology_v2_point_t point;
  gh_topology_v2_cmd_t cmd;
  gh_topology_v2_policy_t pol;
  gh_topology_poll_req_t next_reqs[GH_TOPOLOGY_RUNTIME_MAX_REQS];
  gh_topology_point_binding_t next_points[GH_TOPOLOGY_RUNTIME_MAX_POINTS];
  gh_topology_cmd_binding_t next_cmds[GH_TOPOLOGY_RUNTIME_MAX_CMDS];
  gh_topology_policy_binding_t next_policies[GH_TOPOLOGY_RUNTIME_MAX_POLICIES];
  uint16_t next_count = 0U;
  uint16_t next_point_count = 0U;
  uint16_t next_cmd_count = 0U;
  uint16_t next_policy_count = 0U;
  uint32_t next_mask = 0U;
  uint16_t mod_idx;
  uint16_t req_idx;
  uint16_t point_idx;
  uint16_t cmd_idx;
  uint16_t pol_idx;
  uint32_t req_end;
  uint32_t point_end;
  uint32_t cmd_end;
  config_result_code_t validation_result = CFG_RESULT_IDLE;

  if ((payload == NULL) || !GH_TopologyV2_ValidatePayload(payload, payload_len, &validation_result))
  {
    GH_TopologyRuntime_Clear();
    return false;
  }

  memcpy(&hdr, payload, sizeof(hdr));
  memset(next_reqs, 0, sizeof(next_reqs));
  memset(next_points, 0, sizeof(next_points));
  memset(next_cmds, 0, sizeof(next_cmds));
  memset(next_policies, 0, sizeof(next_policies));

  for (mod_idx = 0U; mod_idx < hdr.module_count; mod_idx++)
  {
    const uint8_t *mod_ptr = &payload[hdr.off_modules + ((uint32_t)mod_idx * sizeof(mod))];

    memcpy(&mod, mod_ptr, sizeof(mod));
    if ((mod.bus_type != GH_TOPOLOGY_RUNTIME_BUS_RTU1) ||
        (mod.slave_id == 0U) ||
        (mod.slave_id > MODBUS_MAX_SLAVES))
    {
      continue;
    }

    req_end = (uint32_t)mod.req_first + (uint32_t)mod.req_count;
    if (req_end > hdr.req_count)
    {
      continue;
    }

    next_mask |= (1UL << (uint32_t)(mod.slave_id - 1U));

    for (req_idx = mod.req_first; req_idx < req_end; req_idx++)
    {
      const uint8_t *req_ptr = &payload[hdr.off_requests + ((uint32_t)req_idx * sizeof(req))];
      gh_topology_poll_req_t *dst;

      memcpy(&req, req_ptr, sizeof(req));
      if ((req.module_id != mod.module_id) ||
          (req.fc != GH_TOPOLOGY_RUNTIME_FC_READ_HOLDING) ||
          (req.reg_count == 0U) ||
          (req.reg_count > MODBUS_MAX_REGS_PER_REQ))
      {
        continue;
      }

      if (next_count >= GH_TOPOLOGY_RUNTIME_MAX_REQS)
      {
        break;
      }

      dst = &next_reqs[next_count];
      dst->req_id = req.req_id;
      dst->module_id = req.module_id;
      dst->slave_id = mod.slave_id;
      dst->start_reg = req.start_reg;
      dst->reg_count = req.reg_count;
      dst->period_ms = (req.period_ms == 0U) ? MODBUS_POLL_PERIOD_MS : req.period_ms;
      dst->timeout_ms = (req.timeout_ms == 0U) ? MODBUS_RTU_RESP_TIMEOUT_MS : req.timeout_ms;
      dst->retries = (req.retries == 0U) ? MODBUS_RETRY_COUNT : req.retries;
      dst->backoff_ms = req.backoff_ms;
      dst->telemetry_word_count = telemetry_word_count_for_req(&req);
      dst->diag_offset = diag_offset_for_req(&req);
      dst->point_first = next_point_count;
      dst->point_count = 0U;

      point_end = (uint32_t)req.point_first + (uint32_t)req.point_count;
      if (point_end > hdr.point_count)
      {
        continue;
      }

      for (point_idx = req.point_first; point_idx < point_end; point_idx++)
      {
        const uint8_t *point_ptr = &payload[hdr.off_points + ((uint32_t)point_idx * sizeof(point))];
        gh_topology_point_binding_t *point_dst;

        if (next_point_count >= GH_TOPOLOGY_RUNTIME_MAX_POINTS)
        {
          break;
        }

        memcpy(&point, point_ptr, sizeof(point));
        if ((point.module_id != mod.module_id) || (point.req_id != req.req_id))
        {
          continue;
        }

        point_dst = &next_points[next_point_count];
        point_dst->slave_id = mod.slave_id;
        point_dst->point_id = point.point_id;
        point_dst->req_id = req.req_id;
        point_dst->module_id = req.module_id;
        point_dst->req_start_reg = req.start_reg;
        point_dst->reg_offset = point.reg_offset;
        point_dst->point_type = point.point_type;
        point_dst->scale_pow10 = point.scale_pow10;
        point_dst->bit_index = point.bit_index;
        point_dst->quality_policy = point.quality_policy;
        point_dst->publish_index = point.publish_index;
        next_point_count++;
        dst->point_count++;
      }

      next_count++;
    }

    cmd_end = (uint32_t)mod.cmd_first + (uint32_t)mod.cmd_count;
    if (cmd_end > hdr.cmd_count)
    {
      continue;
    }

    for (cmd_idx = mod.cmd_first; cmd_idx < cmd_end; cmd_idx++)
    {
      const uint8_t *cmd_ptr = &payload[hdr.off_commands + ((uint32_t)cmd_idx * sizeof(cmd))];
      gh_topology_cmd_binding_t *cmd_dst;

      if (next_cmd_count >= GH_TOPOLOGY_RUNTIME_MAX_CMDS)
      {
        break;
      }

      memcpy(&cmd, cmd_ptr, sizeof(cmd));
      if ((cmd.module_id != mod.module_id) ||
          ((cmd.fc != GH_TOPOLOGY_CMD_FC_WRITE_SINGLE) &&
           (cmd.fc != GH_TOPOLOGY_CMD_FC_WRITE_MULTI)))
      {
        continue;
      }

      cmd_dst = &next_cmds[next_cmd_count];
      cmd_dst->cmd_id = cmd.cmd_id;
      cmd_dst->module_id = cmd.module_id;
      cmd_dst->slave_id = mod.slave_id;
      cmd_dst->fc = cmd.fc;
      cmd_dst->start_reg = cmd.start_reg;
      cmd_dst->reg_count = cmd.max_reg_count;
      cmd_dst->payload_offset = cmd.payload_offset;
      cmd_dst->timeout_ms = (cmd.timeout_ms == 0U) ? MODBUS_RTU_RESP_TIMEOUT_MS : cmd.timeout_ms;
      cmd_dst->retries = (cmd.retries == 0U) ? MODBUS_RETRY_COUNT : cmd.retries;
      cmd_dst->ack_point_id = cmd.ack_point_id;
      cmd_dst->flags = cmd.flags;
      next_cmd_count++;
    }

    for (pol_idx = 0U; pol_idx < hdr.policy_count; pol_idx++)
    {
      const uint8_t *pol_ptr = &payload[hdr.off_policies + ((uint32_t)pol_idx * sizeof(pol))];
      gh_topology_policy_binding_t *pol_dst;

      if (next_policy_count >= GH_TOPOLOGY_RUNTIME_MAX_POLICIES)
      {
        break;
      }

      memcpy(&pol, pol_ptr, sizeof(pol));
      if (pol.module_id != mod.module_id)
      {
        continue;
      }

      pol_dst = &next_policies[next_policy_count];
      pol_dst->module_id = pol.module_id;
      pol_dst->slave_id = mod.slave_id;
      pol_dst->on_timeout = pol.on_timeout;
      pol_dst->on_crc_error = pol.on_crc_error;
      pol_dst->on_link_loss = pol.on_link_loss;
      pol_dst->max_consecutive_fail = pol.max_consecutive_fail;
      pol_dst->recover_good_cycles = pol.recover_good_cycles;
      pol_dst->safe_profile_id = pol.safe_profile_id;
      next_policy_count++;
      break;
    }
  }

  if (!runtime_lock())
  {
    return false;
  }

  memcpy(s_poll_reqs, next_reqs, sizeof(next_reqs));
  memcpy(s_point_bindings, next_points, sizeof(next_points));
  memcpy(s_cmd_bindings, next_cmds, sizeof(next_cmds));
  memcpy(s_policy_bindings, next_policies, sizeof(next_policies));
  s_poll_req_count = next_count;
  s_point_binding_count = next_point_count;
  s_cmd_binding_count = next_cmd_count;
  s_policy_binding_count = next_policy_count;
  s_poll_generation = hdr.generation;
  s_rtu1_slave_mask = next_mask;

  runtime_unlock();
  return true;
}

bool GH_TopologyRuntime_CopyPollPlan(gh_topology_poll_req_t *out_reqs,
                                     uint16_t max_reqs,
                                     uint16_t *out_count,
                                     uint32_t *out_generation,
                                     uint32_t *out_rtu1_slave_mask)
{
  if (out_count == NULL)
  {
    return false;
  }
  if ((out_reqs == NULL) && (max_reqs > 0U))
  {
    return false;
  }

  if (!runtime_lock())
  {
    return false;
  }

  if (s_poll_req_count > max_reqs)
  {
    runtime_unlock();
    return false;
  }

  if (s_poll_req_count > 0U)
  {
    memcpy(out_reqs, s_poll_reqs, (uint32_t)s_poll_req_count * sizeof(gh_topology_poll_req_t));
  }
  *out_count = s_poll_req_count;
  if (out_generation != NULL)
  {
    *out_generation = s_poll_generation;
  }
  if (out_rtu1_slave_mask != NULL)
  {
    *out_rtu1_slave_mask = s_rtu1_slave_mask;
  }

  runtime_unlock();
  return true;
}

bool GH_TopologyRuntime_CopyPointBindings(gh_topology_point_binding_t *out_points,
                                          uint16_t max_points,
                                          uint16_t *out_count,
                                          uint32_t *out_generation)
{
  if (out_count == NULL)
  {
    return false;
  }
  if ((out_points == NULL) && (max_points > 0U))
  {
    return false;
  }

  if (!runtime_lock())
  {
    return false;
  }

  if (s_point_binding_count > max_points)
  {
    runtime_unlock();
    return false;
  }

  if (s_point_binding_count > 0U)
  {
    memcpy(out_points,
           s_point_bindings,
           (uint32_t)s_point_binding_count * sizeof(gh_topology_point_binding_t));
  }
  *out_count = s_point_binding_count;
  if (out_generation != NULL)
  {
    *out_generation = s_poll_generation;
  }

  runtime_unlock();
  return true;
}

bool GH_TopologyRuntime_CopyCommandBindings(gh_topology_cmd_binding_t *out_cmds,
                                            uint16_t max_cmds,
                                            uint16_t *out_count,
                                            uint32_t *out_generation)
{
  if (out_count == NULL)
  {
    return false;
  }
  if ((out_cmds == NULL) && (max_cmds > 0U))
  {
    return false;
  }

  if (!runtime_lock())
  {
    return false;
  }

  if (s_cmd_binding_count > max_cmds)
  {
    runtime_unlock();
    return false;
  }

  if (s_cmd_binding_count > 0U)
  {
    memcpy(out_cmds,
           s_cmd_bindings,
           (uint32_t)s_cmd_binding_count * sizeof(gh_topology_cmd_binding_t));
  }
  *out_count = s_cmd_binding_count;
  if (out_generation != NULL)
  {
    *out_generation = s_poll_generation;
  }

  runtime_unlock();
  return true;
}

bool GH_TopologyRuntime_CopyPolicyBindings(gh_topology_policy_binding_t *out_policies,
                                           uint16_t max_policies,
                                           uint16_t *out_count,
                                           uint32_t *out_generation)
{
  if (out_count == NULL)
  {
    return false;
  }
  if ((out_policies == NULL) && (max_policies > 0U))
  {
    return false;
  }

  if (!runtime_lock())
  {
    return false;
  }

  if (s_policy_binding_count > max_policies)
  {
    runtime_unlock();
    return false;
  }

  if (s_policy_binding_count > 0U)
  {
    memcpy(out_policies,
           s_policy_bindings,
           (uint32_t)s_policy_binding_count * sizeof(gh_topology_policy_binding_t));
  }
  *out_count = s_policy_binding_count;
  if (out_generation != NULL)
  {
    *out_generation = s_poll_generation;
  }

  runtime_unlock();
  return true;
}

static void topo_runtime_clear(void)
{
  GH_TopologyRuntime_Clear();
  g_topology_v2_active = 0U;
  g_topology_v2_ver_major = 0U;
  g_topology_v2_ver_minor = 0U;
  g_topology_v2_generation = 0U;
  g_topology_v2_module_count = 0U;
  g_topology_v2_req_count = 0U;
  g_topology_v2_point_count = 0U;
  g_topology_v2_cmd_count = 0U;
  g_topology_v2_policy_count = 0U;
  g_topology_v2_active_size = 0U;
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

static bool topo_is_rtu_bus(uint8_t bus_type)
{
  return (bus_type == GH_TOPOLOGY_BUS_RTU1);
}

static bool topo_u16_span_fits(uint16_t first, uint16_t count, uint16_t limit)
{
  return (((uint32_t)first + (uint32_t)count) <= (uint32_t)limit);
}

static bool topo_index_in_span(uint16_t index, uint16_t first, uint16_t count)
{
  uint32_t end = (uint32_t)first + (uint32_t)count;
  return ((uint32_t)index >= (uint32_t)first) && ((uint32_t)index < end);
}

static void topo_read_module_row(const uint8_t *payload,
                                 const gh_topology_v2_header_t *hdr,
                                 uint16_t index,
                                 gh_topology_v2_module_t *out)
{
  memcpy(out,
         &payload[hdr->off_modules + ((uint32_t)index * sizeof(gh_topology_v2_module_t))],
         sizeof(gh_topology_v2_module_t));
}

static void topo_read_req_row(const uint8_t *payload,
                              const gh_topology_v2_header_t *hdr,
                              uint16_t index,
                              gh_topology_v2_req_t *out)
{
  memcpy(out,
         &payload[hdr->off_requests + ((uint32_t)index * sizeof(gh_topology_v2_req_t))],
         sizeof(gh_topology_v2_req_t));
}

static void topo_read_point_row(const uint8_t *payload,
                                const gh_topology_v2_header_t *hdr,
                                uint16_t index,
                                gh_topology_v2_point_t *out)
{
  memcpy(out,
         &payload[hdr->off_points + ((uint32_t)index * sizeof(gh_topology_v2_point_t))],
         sizeof(gh_topology_v2_point_t));
}

static void topo_read_cmd_row(const uint8_t *payload,
                              const gh_topology_v2_header_t *hdr,
                              uint16_t index,
                              gh_topology_v2_cmd_t *out)
{
  memcpy(out,
         &payload[hdr->off_commands + ((uint32_t)index * sizeof(gh_topology_v2_cmd_t))],
         sizeof(gh_topology_v2_cmd_t));
}

static void topo_read_policy_row(const uint8_t *payload,
                                 const gh_topology_v2_header_t *hdr,
                                 uint16_t index,
                                 gh_topology_v2_policy_t *out)
{
  memcpy(out,
         &payload[hdr->off_policies + ((uint32_t)index * sizeof(gh_topology_v2_policy_t))],
         sizeof(gh_topology_v2_policy_t));
}

static bool topo_find_module_by_id(const uint8_t *payload,
                                   const gh_topology_v2_header_t *hdr,
                                   uint16_t module_id,
                                   gh_topology_v2_module_t *out_module)
{
  uint16_t i;
  gh_topology_v2_module_t mod;

  for (i = 0U; i < hdr->module_count; i++)
  {
    topo_read_module_row(payload, hdr, i, &mod);
    if (mod.module_id == module_id)
    {
      if (out_module != NULL)
      {
        *out_module = mod;
      }
      return true;
    }
  }
  return false;
}

static bool topo_find_req_by_id(const uint8_t *payload,
                                const gh_topology_v2_header_t *hdr,
                                uint16_t req_id,
                                gh_topology_v2_req_t *out_req,
                                uint16_t *out_req_index)
{
  uint16_t i;
  gh_topology_v2_req_t req;

  for (i = 0U; i < hdr->req_count; i++)
  {
    topo_read_req_row(payload, hdr, i, &req);
    if (req.req_id == req_id)
    {
      if (out_req != NULL)
      {
        *out_req = req;
      }
      if (out_req_index != NULL)
      {
        *out_req_index = i;
      }
      return true;
    }
  }
  return false;
}

static bool topo_find_point_by_id(const uint8_t *payload,
                                  const gh_topology_v2_header_t *hdr,
                                  uint16_t point_id,
                                  gh_topology_v2_point_t *out_point)
{
  uint16_t i;
  gh_topology_v2_point_t point;

  for (i = 0U; i < hdr->point_count; i++)
  {
    topo_read_point_row(payload, hdr, i, &point);
    if (point.point_id == point_id)
    {
      if (out_point != NULL)
      {
        *out_point = point;
      }
      return true;
    }
  }
  return false;
}

static bool topo_reg_windows_overlap(uint16_t start_a,
                                     uint16_t count_a,
                                     uint16_t start_b,
                                     uint16_t count_b)
{
  uint32_t end_a = (uint32_t)start_a + (uint32_t)count_a;
  uint32_t end_b = (uint32_t)start_b + (uint32_t)count_b;

  return !((end_a <= (uint32_t)start_b) || (end_b <= (uint32_t)start_a));
}

static bool topo_validate_semantics(const uint8_t *payload,
                                    const gh_topology_v2_header_t *hdr,
                                    config_result_code_t *out_result)
{
  uint16_t i;
  uint16_t j;
  gh_topology_v2_module_t mod_i;
  gh_topology_v2_module_t mod_j;
  gh_topology_v2_req_t req_i;
  gh_topology_v2_req_t req_j;
  gh_topology_v2_point_t point_i;
  gh_topology_v2_point_t point_j;
  gh_topology_v2_cmd_t cmd_i;
  gh_topology_v2_cmd_t cmd_j;
  gh_topology_v2_policy_t pol_i;
  gh_topology_v2_policy_t pol_j;
  gh_topology_v2_module_t owner_mod;
  gh_topology_v2_req_t owner_req;
  uint16_t owner_req_idx;
  uint8_t cmd_kind;

  for (i = 0U; i < hdr->module_count; i++)
  {
    topo_read_module_row(payload, hdr, i, &mod_i);

    if ((mod_i.bus_type != GH_TOPOLOGY_BUS_RTU1) &&
        (mod_i.bus_type != GH_TOPOLOGY_BUS_TCP))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if (topo_is_rtu_bus(mod_i.bus_type))
    {
      if ((mod_i.slave_id == 0U) || (mod_i.slave_id > GH_TOPOLOGY_RTU_SLAVE_MAX))
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
        return false;
      }
    }
    else if ((mod_i.bus_type == GH_TOPOLOGY_BUS_TCP) && (mod_i.slave_id != 0U))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }

    if (!topo_u16_span_fits(mod_i.req_first, mod_i.req_count, hdr->req_count) ||
        !topo_u16_span_fits(mod_i.cmd_first, mod_i.cmd_count, hdr->cmd_count))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
      return false;
    }

    for (j = 0U; j < i; j++)
    {
      topo_read_module_row(payload, hdr, j, &mod_j);
      if (mod_i.module_id == mod_j.module_id)
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
      if (topo_is_rtu_bus(mod_i.bus_type) &&
          topo_is_rtu_bus(mod_j.bus_type) &&
          (mod_i.bus_type == mod_j.bus_type) &&
          (mod_i.bus_index == mod_j.bus_index) &&
          (mod_i.slave_id == mod_j.slave_id))
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
    }
  }

  for (i = 0U; i < hdr->req_count; i++)
  {
    topo_read_req_row(payload, hdr, i, &req_i);

    if ((req_i.fc != GH_TOPOLOGY_REQ_FC_READ_HOLDING) &&
        (req_i.fc != GH_TOPOLOGY_REQ_FC_READ_INPUT))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if ((req_i.reg_count == 0U) || (req_i.reg_count > MODBUS_MAX_REGS_PER_REQ))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if (!topo_u16_span_fits(req_i.point_first, req_i.point_count, hdr->point_count))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
      return false;
    }
    if (!topo_find_module_by_id(payload, hdr, req_i.module_id, &owner_mod))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if (!topo_index_in_span(i, owner_mod.req_first, owner_mod.req_count))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
      return false;
    }
    if ((owner_mod.bus_type == GH_TOPOLOGY_BUS_RTU1) &&
        (owner_mod.slave_id >= 1U) &&
        (owner_mod.slave_id <= MODBUS_MAX_SLAVES) &&
        (req_i.fc != GH_TOPOLOGY_REQ_FC_READ_HOLDING))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }

    for (j = 0U; j < i; j++)
    {
      topo_read_req_row(payload, hdr, j, &req_j);
      if (req_i.req_id == req_j.req_id)
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
      if ((req_i.module_id == req_j.module_id) &&
          topo_reg_windows_overlap(req_i.start_reg, req_i.reg_count, req_j.start_reg, req_j.reg_count))
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
    }
  }

  for (i = 0U; i < hdr->point_count; i++)
  {
    uint8_t point_words;

    topo_read_point_row(payload, hdr, i, &point_i);

    if ((point_i.point_type < GH_TOPOLOGY_POINT_TYPE_U16) ||
        (point_i.point_type > GH_TOPOLOGY_POINT_TYPE_BIT))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if ((point_i.point_type == GH_TOPOLOGY_POINT_TYPE_BIT) && (point_i.bit_index > 15U))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if (point_i.publish_index >= SENSOR_COUNT)
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BUDGET;
      return false;
    }
    if (!topo_find_module_by_id(payload, hdr, point_i.module_id, NULL))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if (!topo_find_req_by_id(payload, hdr, point_i.req_id, &owner_req, &owner_req_idx))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if (!topo_index_in_span(i, owner_req.point_first, owner_req.point_count) ||
        (owner_req.module_id != point_i.module_id) ||
        (point_i.reg_offset >= owner_req.reg_count))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
      return false;
    }
    point_words = topo_point_word_width(point_i.point_type);
    if ((point_words == 0U) ||
        (((uint32_t)point_i.reg_offset + (uint32_t)point_words) > (uint32_t)owner_req.reg_count))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
      return false;
    }
    if (!topo_find_module_by_id(payload, hdr, owner_req.module_id, &owner_mod) ||
        !topo_index_in_span(owner_req_idx, owner_mod.req_first, owner_mod.req_count))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
      return false;
    }

    for (j = 0U; j < i; j++)
    {
      topo_read_point_row(payload, hdr, j, &point_j);
      if ((point_i.point_id == point_j.point_id) || (point_i.publish_index == point_j.publish_index))
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
    }
  }

  for (i = 0U; i < hdr->cmd_count; i++)
  {
    topo_read_cmd_row(payload, hdr, i, &cmd_i);
    cmd_kind = GH_TOPOLOGY_CMD_KIND_FROM_FLAGS(cmd_i.flags);

    if (cmd_kind > GH_TOPOLOGY_CMD_KIND_MAX)
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }

    if ((cmd_i.fc != GH_TOPOLOGY_CMD_FC_WRITE_SINGLE) &&
        (cmd_i.fc != GH_TOPOLOGY_CMD_FC_WRITE_MULTI))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if ((cmd_i.max_reg_count == 0U) || (cmd_i.max_reg_count > MODBUS_MAX_REGS_PER_REQ))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if ((cmd_i.fc == GH_TOPOLOGY_CMD_FC_WRITE_SINGLE) && (cmd_i.max_reg_count != 1U))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if ((cmd_kind == GH_TOPOLOGY_CMD_KIND_SCHEDULE) &&
        (cmd_i.fc == GH_TOPOLOGY_CMD_FC_WRITE_MULTI) &&
        (cmd_i.max_reg_count != GH_TOPOLOGY_RUNTIME_SCHED_WORDS))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if (!topo_find_module_by_id(payload, hdr, cmd_i.module_id, &owner_mod))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if (!topo_index_in_span(i, owner_mod.cmd_first, owner_mod.cmd_count))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
      return false;
    }
    if ((owner_mod.bus_type == GH_TOPOLOGY_BUS_RTU1) &&
        !topo_u16_span_fits(cmd_i.payload_offset, cmd_i.max_reg_count, GH_TOPOLOGY_RUNTIME_CMD_WORDS))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
      return false;
    }
    if (cmd_i.ack_point_id != 0U)
    {
      gh_topology_v2_point_t ack_point;

      if (!topo_find_point_by_id(payload, hdr, cmd_i.ack_point_id, &ack_point) ||
          (ack_point.module_id != cmd_i.module_id))
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
        return false;
      }
    }

    for (j = 0U; j < i; j++)
    {
      topo_read_cmd_row(payload, hdr, j, &cmd_j);
      if (cmd_i.cmd_id == cmd_j.cmd_id)
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
      if ((cmd_i.module_id == cmd_j.module_id) &&
          topo_reg_windows_overlap(cmd_i.start_reg, cmd_i.max_reg_count, cmd_j.start_reg, cmd_j.max_reg_count))
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
    }
  }

  for (i = 0U; i < hdr->policy_count; i++)
  {
    topo_read_policy_row(payload, hdr, i, &pol_i);
    if (!topo_find_module_by_id(payload, hdr, pol_i.module_id, NULL))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }
    if ((pol_i.on_timeout > GH_TOPOLOGY_POLICY_ACTION_MAX) ||
        (pol_i.on_crc_error > GH_TOPOLOGY_POLICY_ACTION_MAX) ||
        (pol_i.on_link_loss > GH_TOPOLOGY_POLICY_ACTION_MAX))
    {
      *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
      return false;
    }

    for (j = 0U; j < i; j++)
    {
      topo_read_policy_row(payload, hdr, j, &pol_j);
      if (pol_i.module_id == pol_j.module_id)
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
    }
  }

  return true;
}

static bool topo_validate_poll_budget(const uint8_t *payload,
                                      const gh_topology_v2_header_t *hdr)
{
  uint16_t i;
  gh_topology_v2_req_t req;
  gh_topology_v2_module_t mod;
  uint16_t retries;
  uint16_t period_ms;
  uint16_t timeout_ms;
  uint32_t req_worst_ms;
  uint64_t util_permille = 0ULL;

  for (i = 0U; i < hdr->req_count; i++)
  {
    topo_read_req_row(payload, hdr, i, &req);
    if (!topo_find_module_by_id(payload, hdr, req.module_id, &mod))
    {
      return false;
    }
    if ((mod.bus_type != GH_TOPOLOGY_BUS_RTU1) ||
        (mod.slave_id == 0U) ||
        (mod.slave_id > MODBUS_MAX_SLAVES) ||
        (req.fc != GH_TOPOLOGY_REQ_FC_READ_HOLDING))
    {
      continue;
    }

    retries = (req.retries == 0U) ? MODBUS_RETRY_COUNT : req.retries;
    period_ms = (req.period_ms == 0U) ? MODBUS_POLL_PERIOD_MS : req.period_ms;
    timeout_ms = (req.timeout_ms == 0U) ? MODBUS_RTU_RESP_TIMEOUT_MS : req.timeout_ms;
    if (period_ms == 0U)
    {
      return false;
    }

    req_worst_ms = ((uint32_t)retries * ((uint32_t)MODBUS_UART_TX_TIMEOUT_MS + (uint32_t)timeout_ms));
    if (retries > 1U)
    {
      req_worst_ms += ((uint32_t)(retries - 1U) * (uint32_t)req.backoff_ms);
    }
    req_worst_ms += MODBUS_INTER_SLAVE_DELAY_MS;
    util_permille += (((uint64_t)req_worst_ms * 1000ULL) + ((uint64_t)period_ms - 1ULL)) / (uint64_t)period_ms;

    if (util_permille > 1000ULL)
    {
      return false;
    }
  }

  return true;
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

  if (!topo_validate_semantics(payload, &hdr, out_result))
  {
    return false;
  }
  if (!topo_validate_poll_budget(payload, &hdr))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_BUDGET;
    return false;
  }

  *out_result = CFG_RESULT_IDLE;
  return true;
}

void GH_TopologyV2_SyncRuntimeFromPayload(const uint8_t *payload, uint32_t payload_len)
{
  gh_topology_v2_header_t hdr;
  config_result_code_t result = CFG_RESULT_IDLE;

  topo_runtime_clear();

  if (!GH_TopologyV2_ValidatePayload(payload, payload_len, &result))
  {
    return;
  }

  memcpy(&hdr, payload, sizeof(hdr));
  (void)GH_TopologyRuntime_RebuildFromPayload(payload, payload_len);
  g_topology_v2_active = 1U;
  g_topology_v2_ver_major = hdr.ver_major;
  g_topology_v2_ver_minor = hdr.ver_minor;
  g_topology_v2_generation = hdr.generation;
  g_topology_v2_module_count = hdr.module_count;
  g_topology_v2_req_count = hdr.req_count;
  g_topology_v2_point_count = hdr.point_count;
  g_topology_v2_cmd_count = hdr.cmd_count;
  g_topology_v2_policy_count = hdr.policy_count;
  g_topology_v2_active_size = hdr.total_size;
}

void GH_TopologyV2_SyncRuntimeFromConfig(const active_config_t *cfg)
{
  if (cfg == NULL)
  {
    topo_runtime_clear();
    return;
  }

  GH_TopologyV2_SyncRuntimeFromPayload(cfg->payload, CONFIG_PAYLOAD_SIZE);
}

static void topology_staging_reset(void)
{
  s_topology_staging_size = 0U;
  s_topology_staging_generation = 0U;
  s_topology_staging_chunks_mask = 0U;
  memset(s_topology_staging_blob, 0, sizeof(s_topology_staging_blob));
}

static uint8_t topology_required_chunks(uint32_t total_size)
{
  if (total_size == 0U)
  {
    return 0U;
  }
  return (uint8_t)((total_size + TOPOLOGY_UPLOAD_CHUNK_BYTES - 1U) / TOPOLOGY_UPLOAD_CHUNK_BYTES);
}

static bool topology_all_chunks_received(uint32_t total_size)
{
  uint8_t required = topology_required_chunks(total_size);
  uint32_t required_mask;

  if (required == 0U)
  {
    return false;
  }
  if (required >= 32U)
  {
    return false;
  }

  required_mask = (1UL << required) - 1UL;
  return ((s_topology_staging_chunks_mask & required_mask) == required_mask);
}

static bool topo_flash_write_words(uint32_t addr, const uint8_t *data, uint32_t len)
{
  uint32_t i;
  uint32_t word;
  HAL_StatusTypeDef st;

  for (i = 0U; i < len; i += 4U)
  {
    word = 0xFFFFFFFFUL;
    memcpy(&word, &data[i], ((len - i) >= 4U) ? 4U : (len - i));
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word);
    if (st != HAL_OK)
    {
      return false;
    }
  }
  return true;
}

static bool topology_write_payload(uint32_t topo_slot_addr,
                                   const uint8_t *blob,
                                   uint32_t total_size,
                                   uint32_t generation)
{
  topology_slot_header_t hdr = {0};
  bool ok;
  uint32_t marker_addr;

  if ((blob == NULL) || (total_size == 0U) || (total_size > TOPOLOGY_MAX_BLOB_SIZE))
  {
    return false;
  }

  hdr.total_size = total_size;
  hdr.generation = generation;
  hdr.blob_crc = gh_crc32_compute(blob, total_size);
  hdr.valid_marker = 0xFFFFFFFFUL;

  HAL_FLASH_Unlock();

  ok = topo_flash_write_words(topo_slot_addr, (const uint8_t *)&hdr, sizeof(hdr));
  ok = ok && topo_flash_write_words(topo_slot_addr + sizeof(hdr), blob, total_size);
  if (ok)
  {
    marker_addr = topo_slot_addr + (uint32_t)offsetof(topology_slot_header_t, valid_marker);
    ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, marker_addr, TOPOLOGY_VALID_MARKER) == HAL_OK);
  }

  HAL_FLASH_Lock();
  return ok;
}

static bool topology_slot_read(uint32_t topo_slot_addr,
                               uint8_t *out_blob,
                               uint32_t *out_size,
                               uint32_t *out_generation)
{
  const topology_slot_header_t *hdr = (const topology_slot_header_t *)topo_slot_addr;
  const uint8_t *blob = (const uint8_t *)(topo_slot_addr + sizeof(topology_slot_header_t));
  config_result_code_t validation_result = CFG_RESULT_IDLE;

  if ((out_blob == NULL) || (out_size == NULL) || (out_generation == NULL))
  {
    return false;
  }

  if (hdr->valid_marker != TOPOLOGY_VALID_MARKER)
  {
    return false;
  }
  if ((hdr->total_size < sizeof(gh_topology_v2_header_t)) || (hdr->total_size > TOPOLOGY_MAX_BLOB_SIZE))
  {
    return false;
  }
  if (gh_crc32_compute(blob, hdr->total_size) != hdr->blob_crc)
  {
    return false;
  }
  if (!GH_TopologyV2_ValidatePayload(blob, hdr->total_size, &validation_result))
  {
    return false;
  }

  memcpy(out_blob, blob, hdr->total_size);
  *out_size = hdr->total_size;
  *out_generation = hdr->generation;
  return true;
}

static bool cfg_write_slot_with_retries(uint32_t slot_addr,
                                        uint32_t sector,
                                        const active_config_t *cfg)
{
  uint8_t attempt;
  bool ok;

  for (attempt = 0U; attempt < TOPOLOGY_FLASH_WRITE_RETRIES; attempt++)
  {
    ok = config_write_to_slot(slot_addr, sector, cfg);
    if (ok && s_topology_active_valid)
    {
      ok = topology_write_payload(slot_addr + TOPOLOGY_SLOT_OFFSET,
                                  s_topology_active_blob,
                                  s_topology_active_size,
                                  s_topology_active_generation);
    }
    if (ok)
    {
      return true;
    }
    if ((attempt + 1U) < TOPOLOGY_FLASH_WRITE_RETRIES)
    {
      osDelay(TOPOLOGY_FLASH_RETRY_DELAY_MS);
    }
  }

  return false;
}

static bool topology_write_slot_with_retries(uint32_t slot_addr,
                                             uint32_t sector,
                                             const active_config_t *legacy_cfg,
                                             const uint8_t *blob,
                                             uint32_t total_size,
                                             uint32_t generation)
{
  uint8_t attempt;
  bool ok;

  for (attempt = 0U; attempt < CONFIG_FLASH_WRITE_RETRIES; attempt++)
  {
    ok = config_write_to_slot(slot_addr, sector, legacy_cfg);
    if (ok)
    {
      ok = topology_write_payload(slot_addr + TOPOLOGY_SLOT_OFFSET, blob, total_size, generation);
    }
    if (ok)
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

static bool topology_store_ab_with_fallback(bool prefer_slot_a,
                                            const active_config_t *legacy_cfg,
                                            const uint8_t *blob,
                                            uint32_t total_size,
                                            uint32_t generation,
                                            bool *out_slot_a_used)
{
  bool ok;

  if ((legacy_cfg == NULL) || (blob == NULL) || (out_slot_a_used == NULL))
  {
    return false;
  }

  if (prefer_slot_a)
  {
    ok = topology_write_slot_with_retries(CONFIG_SLOT_A_ADDR,
                                          CONFIG_SLOT_A_SECTOR,
                                          legacy_cfg,
                                          blob,
                                          total_size,
                                          generation);
    if (ok)
    {
      *out_slot_a_used = true;
      return true;
    }
    ok = topology_write_slot_with_retries(CONFIG_SLOT_B_ADDR,
                                          CONFIG_SLOT_B_SECTOR,
                                          legacy_cfg,
                                          blob,
                                          total_size,
                                          generation);
    if (ok)
    {
      *out_slot_a_used = false;
      return true;
    }
  }
  else
  {
    ok = topology_write_slot_with_retries(CONFIG_SLOT_B_ADDR,
                                          CONFIG_SLOT_B_SECTOR,
                                          legacy_cfg,
                                          blob,
                                          total_size,
                                          generation);
    if (ok)
    {
      *out_slot_a_used = false;
      return true;
    }
    ok = topology_write_slot_with_retries(CONFIG_SLOT_A_ADDR,
                                          CONFIG_SLOT_A_SECTOR,
                                          legacy_cfg,
                                          blob,
                                          total_size,
                                          generation);
    if (ok)
    {
      *out_slot_a_used = true;
      return true;
    }
  }

  return false;
}

void GH_TopologyStorage_LoadActiveFromFlash(void)
{
  uint8_t blob_a[TOPOLOGY_MAX_BLOB_SIZE];
  uint8_t blob_b[TOPOLOGY_MAX_BLOB_SIZE];
  uint32_t size_a = 0U;
  uint32_t size_b = 0U;
  uint32_t gen_a = 0U;
  uint32_t gen_b = 0U;
  bool valid_a;
  bool valid_b;

  valid_a = topology_slot_read(TOPOLOGY_SLOT_A_ADDR, blob_a, &size_a, &gen_a);
  valid_b = topology_slot_read(TOPOLOGY_SLOT_B_ADDR, blob_b, &size_b, &gen_b);

  if (valid_a && (!valid_b || (gen_a >= gen_b)))
  {
    memcpy(s_topology_active_blob, blob_a, size_a);
    s_topology_active_size = size_a;
    s_topology_active_generation = gen_a;
    s_topology_active_valid = true;
    s_topology_prefer_slot_a = false;
    GH_TopologyV2_SyncRuntimeFromPayload(s_topology_active_blob, s_topology_active_size);
    return;
  }

  if (valid_b)
  {
    memcpy(s_topology_active_blob, blob_b, size_b);
    s_topology_active_size = size_b;
    s_topology_active_generation = gen_b;
    s_topology_active_valid = true;
    s_topology_prefer_slot_a = true;
    GH_TopologyV2_SyncRuntimeFromPayload(s_topology_active_blob, s_topology_active_size);
    return;
  }

  s_topology_active_valid = false;
  s_topology_active_size = 0U;
  s_topology_active_generation = 0U;
  s_topology_prefer_slot_a = true;
  topo_runtime_clear();
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
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
    return false;
  }

  if (!GH_ConfigStorage_PayloadValuesValid(req->payload))
  {
    *out_result = CFG_RESULT_REJECT_RANGE;
    return false;
  }

  *out_result = CFG_RESULT_IDLE;
  return true;
}

static config_result_code_t topology_process_chunk(const topology_chunk_req_t *req)
{
  uint8_t chunk_bytes[TOPOLOGY_UPLOAD_CHUNK_BYTES];
  uint32_t chunk_bytes_len = 0U;
  uint32_t chunk_offset;
  uint32_t chunk_crc;
  uint8_t required_chunks;
  uint16_t i;
  config_result_code_t validation_result = CFG_RESULT_IDLE;
  bool written_slot_a = false;
  bool ok;

  if (req == NULL)
  {
    return CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
  }

  if ((req->flags & TOPOLOGY_UPLOAD_FLAG_RESET) != 0U)
  {
    topology_staging_reset();
  }

  if ((req->total_size < sizeof(gh_topology_v2_header_t)) || (req->total_size > TOPOLOGY_MAX_BLOB_SIZE))
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BUDGET;
  }

  required_chunks = topology_required_chunks(req->total_size);
  if ((required_chunks == 0U) || (required_chunks >= 32U))
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BUDGET;
  }
  if (req->chunk_index >= required_chunks)
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
  }
  if (req->chunk_words > TOPOLOGY_UPLOAD_CHUNK_WORDS)
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
  }

  if ((s_topology_staging_size == 0U) ||
      (s_topology_staging_size != req->total_size) ||
      (s_topology_staging_generation != req->generation))
  {
    if (((req->flags & TOPOLOGY_UPLOAD_FLAG_RESET) == 0U) && (req->chunk_index != 0U))
    {
      return CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
    }
    topology_staging_reset();
    s_topology_staging_size = req->total_size;
    s_topology_staging_generation = req->generation;
  }

  chunk_bytes_len = (uint32_t)req->chunk_words * 2U;
  chunk_offset = (uint32_t)req->chunk_index * TOPOLOGY_UPLOAD_CHUNK_BYTES;
  if ((chunk_offset + chunk_bytes_len) > req->total_size)
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
  }

  for (i = 0U; i < req->chunk_words; i++)
  {
    chunk_bytes[2U * i] = (uint8_t)(req->chunk_data[i] >> 8U);
    chunk_bytes[(2U * i) + 1U] = (uint8_t)(req->chunk_data[i] & 0x00FFU);
  }

  if (chunk_bytes_len > 0U)
  {
    chunk_crc = gh_crc32_compute(chunk_bytes, chunk_bytes_len);
    if (chunk_crc != req->chunk_crc)
    {
      return CFG_RESULT_REJECT_TOPOLOGY_CRC;
    }

    memcpy(&s_topology_staging_blob[chunk_offset], chunk_bytes, chunk_bytes_len);
    s_topology_staging_chunks_mask |= (1UL << req->chunk_index);
  }

  if ((req->flags & TOPOLOGY_UPLOAD_FLAG_COMMIT) == 0U)
  {
    return CFG_RESULT_QUEUED;
  }

  if (!topology_all_chunks_received(req->total_size))
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
  }

  if (!GH_TopologyV2_ValidatePayload(s_topology_staging_blob, req->total_size, &validation_result))
  {
    return validation_result;
  }

  ok = topology_store_ab_with_fallback(s_topology_prefer_slot_a,
                                       &g_active_config,
                                       s_topology_staging_blob,
                                       req->total_size,
                                       req->generation,
                                       &written_slot_a);
  if (!ok)
  {
    return CFG_RESULT_FLASH_FAIL;
  }

  s_topology_prefer_slot_a = !written_slot_a;
  memcpy(s_topology_active_blob, s_topology_staging_blob, req->total_size);
  s_topology_active_size = req->total_size;
  s_topology_active_generation = req->generation;
  s_topology_active_valid = true;
  GH_TopologyV2_SyncRuntimeFromPayload(s_topology_active_blob, s_topology_active_size);
  topology_staging_reset();

  return CFG_RESULT_APPLIED;
}

void GH_ConfigStorageTask_Run(void *argument)
{
  config_update_req_t req = {0};
  topology_chunk_req_t topo_req = {0};
  config_apply_req_t apply_req = {0};
  active_config_t pending = {0};
  bool prefer_slot_a = true;
  bool written_slot_a = false;
  bool ok;
  uint8_t attempt;
  config_result_code_t validation_result = CFG_RESULT_IDLE;
  config_result_code_t topo_result = CFG_RESULT_IDLE;
  (void)argument;

  for (;;)
  {
    if (osMessageQueueGet(qTopologyStoreHandle, &topo_req, NULL, 0U) == osOK)
    {
      if ((topo_req.flags & TOPOLOGY_UPLOAD_FLAG_COMMIT) != 0U)
      {
        g_topology_commit_in_progress = 1U;
      }
      topo_result = topology_process_chunk(&topo_req);
      if ((topo_req.flags & TOPOLOGY_UPLOAD_FLAG_COMMIT) != 0U)
      {
        g_topology_commit_in_progress = 0U;
      }
      GH_ModbusMap_ReportTopologyResult(topo_req.request_token,
                                        topo_result,
                                        g_topology_v2_generation,
                                        g_topology_v2_active_size);

      if ((topo_result != CFG_RESULT_QUEUED) && (topo_result != CFG_RESULT_APPLIED))
      {
        publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)topo_result);
      }
      else if (topo_result == CFG_RESULT_APPLIED)
      {
        publish_event(EVENT_SEV_INFO, EVENT_CODE_CFG_APPLIED, 0U, (float)g_topology_v2_generation);
      }
    }

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
