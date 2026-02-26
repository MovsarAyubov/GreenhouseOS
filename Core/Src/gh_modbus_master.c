#include "gh_modbus_master.h"

#include "gh_runtime_state.h"
#include "gh_modbus_map.h"
#include "gh_topology_runtime.h"
#include "gh_topology_v2.h"

#include <math.h>
#include <string.h>

#define GH_RTU_SLAVE_FIRST            1U
#define GH_RTU_SLAVE_LAST             MODBUS_MAX_SLAVES
#define GH_RTU_READ_START_REG         0U
#define GH_RTU_SENSORS_PER_SLAVE      9U
#define GH_RTU_READ_REG_COUNT         9U
#define GH_RTU_DIAG_BASE_REG          128U
#define GH_RTU_DIAG_COUNT             6U
#define GH_RTU_CTRL_MODE_CMD_REG      102U
#define GH_RTU_CTRL_SP_WATER_RAIL_REG 106U
#define GH_RTU_CTRL_SP_WATER_GROW_REG 107U
#define GH_RTU_CTRL_SP_WATER_UPPER_REG 108U
#define GH_RTU_CTRL_SP_WATER_UNDER_REG 109U
#define GH_RTU_APPLY_TRIGGER_REG      122U
#define GH_QUALITY_RECOVER_OK_CYCLES  2U
#define GH_QUALITY_FAIL_STALE_CYCLES  2U

#define GH_POINT_TYPE_U16             1U
#define GH_POINT_TYPE_S16             2U
#define GH_POINT_TYPE_U32             3U
#define GH_POINT_TYPE_S32             4U
#define GH_POINT_TYPE_FLOAT           5U
#define GH_POINT_TYPE_BIT             6U
#define GH_CMD_FC_WRITE_SINGLE        6U
#define GH_CMD_FC_WRITE_MULTI         16U

static gh_topology_poll_req_t s_topology_plan_cache[GH_TOPOLOGY_V2_MAX_REQ_PROFILES] __attribute__((section(".ccmram")));
static gh_topology_point_binding_t s_topology_point_cache[GH_TOPOLOGY_V2_MAX_POINTS] __attribute__((section(".ccmram")));
static gh_topology_cmd_binding_t s_topology_cmd_cache[GH_TOPOLOGY_V2_MAX_COMMANDS] __attribute__((section(".ccmram")));

static uint8_t gh_slave_to_index(uint8_t slave_id)
{
  return (uint8_t)(slave_id - 1U);
}

static bool gh_slave_id_valid(uint8_t slave_id)
{
  return (slave_id >= GH_RTU_SLAVE_FIRST) && (slave_id <= GH_RTU_SLAVE_LAST);
}

static uint8_t gh_quality_fail_offline_cycles(void)
{
  uint32_t cycles = (MODBUS_OFFLINE_REPROBE_MS + MODBUS_POLL_PERIOD_MS - 1U) / MODBUS_POLL_PERIOD_MS;

  if (cycles < GH_QUALITY_FAIL_STALE_CYCLES)
  {
    cycles = GH_QUALITY_FAIL_STALE_CYCLES;
  }
  if (cycles > 255U)
  {
    cycles = 255U;
  }
  return (uint8_t)cycles;
}

static bool gh_slave_enabled(uint8_t slave_id)
{
  uint32_t bit;

  if (!gh_slave_id_valid(slave_id))
  {
    return false;
  }

  bit = (1UL << (uint32_t)(slave_id - 1U));
  return ((MODBUS_ENABLED_SLAVE_MASK & bit) != 0U);
}

static bool gh_modbus_read_holding_retry(uint8_t slave_id,
                                         uint16_t start_reg,
                                         uint16_t reg_count,
                                         uint16_t *out_regs,
                                         uint32_t timeout_ms,
                                         uint8_t retries,
                                         uint8_t backoff_ms)
{
  uint8_t attempt;
  uint8_t retry_count;

  retry_count = (retries == 0U) ? MODBUS_RETRY_COUNT : retries;
  if (timeout_ms == 0U)
  {
    timeout_ms = MODBUS_RTU_RESP_TIMEOUT_MS;
  }

  for (attempt = 0U; attempt < retry_count; attempt++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if (modbus_read_holding_registers_timeout(slave_id,
                                              start_reg,
                                              reg_count,
                                              out_regs,
                                              timeout_ms))
    {
      task_heartbeat_kick(TASK_BIT_MODBUS);
      return true;
    }
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if ((attempt + 1U) < retry_count)
    {
      osDelay((uint32_t)backoff_ms * (attempt + 1U));
    }
  }

  return false;
}

static bool gh_modbus_write_single_retry(uint8_t slave_id,
                                         uint16_t reg,
                                         uint16_t value,
                                         uint8_t retries,
                                         uint32_t timeout_ms)
{
  uint8_t attempt;
  uint8_t retry_count;

  retry_count = (retries == 0U) ? MODBUS_RETRY_COUNT : retries;
  if (timeout_ms == 0U)
  {
    timeout_ms = MODBUS_RTU_RESP_TIMEOUT_MS;
  }

  for (attempt = 0U; attempt < retry_count; attempt++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if (modbus_write_single_holding_register_timeout(slave_id, reg, value, timeout_ms))
    {
      task_heartbeat_kick(TASK_BIT_MODBUS);
      return true;
    }
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if ((attempt + 1U) < retry_count)
    {
      osDelay(MODBUS_RETRY_BACKOFF_MS * (attempt + 1U));
    }
  }

  return false;
}

static bool gh_modbus_write_multi_retry(uint8_t slave_id,
                                        uint16_t start_reg,
                                        uint16_t reg_count,
                                        const uint16_t *regs,
                                        uint8_t retries,
                                        uint32_t timeout_ms)
{
  uint8_t attempt;
  uint8_t retry_count;

  if ((regs == NULL) || (reg_count == 0U) || (reg_count > MODBUS_MAX_REGS_PER_REQ))
  {
    return false;
  }

  retry_count = (retries == 0U) ? MODBUS_RETRY_COUNT : retries;
  if (timeout_ms == 0U)
  {
    timeout_ms = MODBUS_RTU_RESP_TIMEOUT_MS;
  }

  for (attempt = 0U; attempt < retry_count; attempt++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if (modbus_write_multiple_holding_registers_timeout(slave_id,
                                                        start_reg,
                                                        reg_count,
                                                        regs,
                                                        timeout_ms))
    {
      task_heartbeat_kick(TASK_BIT_MODBUS);
      return true;
    }
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if ((attempt + 1U) < retry_count)
    {
      osDelay(MODBUS_RETRY_BACKOFF_MS * (attempt + 1U));
    }
  }

  return false;
}

static void gh_set_slave_quality(uint8_t slave_id, uint8_t quality)
{
  uint16_t i;
  uint16_t global_idx;

  if (!gh_slave_id_valid(slave_id))
  {
    return;
  }

  for (i = 0U; i < GH_RTU_SENSORS_PER_SLAVE; i++)
  {
    global_idx = (uint16_t)(((uint16_t)(slave_id - 1U) * GH_RTU_SENSORS_PER_SLAVE) + i);
    if (global_idx < SENSOR_COUNT)
    {
      g_sensors[global_idx].quality = quality;
    }
  }
}

static void gh_set_points_quality_for_slave(uint8_t slave_id,
                                            uint8_t quality,
                                            const gh_topology_point_binding_t *points,
                                            uint16_t point_count)
{
  uint16_t i;
  uint16_t publish_idx;

  if ((points == NULL) || (point_count == 0U))
  {
    return;
  }

  for (i = 0U; i < point_count; i++)
  {
    if (points[i].slave_id != slave_id)
    {
      continue;
    }
    publish_idx = points[i].publish_index;
    if (publish_idx < SENSOR_COUNT)
    {
      g_sensors[publish_idx].quality = quality;
    }
  }
}

static bool gh_decode_point_value(const uint16_t *regs,
                                  uint16_t reg_count,
                                  const gh_topology_point_binding_t *point,
                                  float *out_value)
{
  uint16_t off;
  float value = 0.0f;
  uint32_t u32v;
  int32_t s32v;

  if ((regs == NULL) || (point == NULL) || (out_value == NULL))
  {
    return false;
  }

  off = point->reg_offset;
  if (off >= reg_count)
  {
    return false;
  }

  switch (point->point_type)
  {
    case GH_POINT_TYPE_U16:
      value = (float)regs[off];
      break;
    case GH_POINT_TYPE_S16:
      value = (float)(int16_t)regs[off];
      break;
    case GH_POINT_TYPE_U32:
      if ((off + 1U) >= reg_count)
      {
        return false;
      }
      u32v = ((uint32_t)regs[off] << 16U) | (uint32_t)regs[off + 1U];
      value = (float)u32v;
      break;
    case GH_POINT_TYPE_S32:
      if ((off + 1U) >= reg_count)
      {
        return false;
      }
      s32v = (int32_t)(((uint32_t)regs[off] << 16U) | (uint32_t)regs[off + 1U]);
      value = (float)s32v;
      break;
    case GH_POINT_TYPE_FLOAT:
      if ((off + 1U) >= reg_count)
      {
        return false;
      }
      u32v = ((uint32_t)regs[off] << 16U) | (uint32_t)regs[off + 1U];
      memcpy(&value, &u32v, sizeof(value));
      break;
    case GH_POINT_TYPE_BIT:
      if (point->bit_index > 15U)
      {
        return false;
      }
      value = ((regs[off] & (uint16_t)(1U << point->bit_index)) != 0U) ? 1.0f : 0.0f;
      break;
    default:
      return false;
  }

  if (point->scale_pow10 != 0)
  {
    value *= powf(10.0f, (float)point->scale_pow10);
  }

  *out_value = value;
  return true;
}

static void gh_publish_points_from_request(const gh_topology_poll_req_t *req,
                                           const uint16_t *regs,
                                           uint32_t now_ms,
                                           const gh_topology_point_binding_t *points,
                                           uint16_t point_count)
{
  uint16_t i;
  uint16_t end;
  float value = 0.0f;
  uint16_t publish_idx;

  if ((req == NULL) || (regs == NULL) || (points == NULL) || (point_count == 0U))
  {
    return;
  }
  if (req->point_count == 0U)
  {
    return;
  }
  if (req->point_first >= point_count)
  {
    return;
  }

  end = (uint16_t)(req->point_first + req->point_count);
  if (end > point_count)
  {
    end = point_count;
  }

  for (i = req->point_first; i < end; i++)
  {
    if (!gh_decode_point_value(regs, req->reg_count, &points[i], &value))
    {
      continue;
    }
    publish_idx = points[i].publish_index;
    if (publish_idx < SENSOR_COUNT)
    {
      g_sensors[publish_idx].value = value;
      g_sensors[publish_idx].timestamp_ms = now_ms;
    }
  }
}

static uint8_t gh_point_word_width(uint8_t point_type)
{
  switch (point_type)
  {
    case GH_POINT_TYPE_U16:
    case GH_POINT_TYPE_S16:
    case GH_POINT_TYPE_BIT:
      return 1U;
    case GH_POINT_TYPE_U32:
    case GH_POINT_TYPE_S32:
    case GH_POINT_TYPE_FLOAT:
      return 2U;
    default:
      return 0U;
  }
}

static const gh_topology_point_binding_t *gh_find_point_binding_by_id(const gh_topology_point_binding_t *points,
                                                                      uint16_t point_count,
                                                                      uint16_t point_id,
                                                                      uint8_t slave_id)
{
  uint16_t i;

  if ((points == NULL) || (point_count == 0U) || (point_id == 0U))
  {
    return NULL;
  }

  for (i = 0U; i < point_count; i++)
  {
    if ((points[i].point_id == point_id) && (points[i].slave_id == slave_id))
    {
      return &points[i];
    }
  }

  return NULL;
}

static bool gh_command_ack_probe(uint8_t slave_id,
                                 const gh_topology_cmd_binding_t *cmd,
                                 const gh_topology_point_binding_t *points,
                                 uint16_t point_count)
{
  const gh_topology_point_binding_t *ack_point;
  uint16_t ack_regs[2] = {0U, 0U};
  uint8_t words;
  uint32_t reg_addr;

  if ((cmd == NULL) || (cmd->ack_point_id == 0U))
  {
    return true;
  }

  ack_point = gh_find_point_binding_by_id(points, point_count, cmd->ack_point_id, slave_id);
  if (ack_point == NULL)
  {
    return false;
  }

  words = gh_point_word_width(ack_point->point_type);
  if ((words == 0U) || (words > 2U))
  {
    return false;
  }

  reg_addr = (uint32_t)ack_point->req_start_reg + (uint32_t)ack_point->reg_offset;
  if (reg_addr > 0xFFFFU)
  {
    return false;
  }

  return gh_modbus_read_holding_retry(slave_id,
                                      (uint16_t)reg_addr,
                                      words,
                                      ack_regs,
                                      cmd->timeout_ms,
                                      cmd->retries,
                                      MODBUS_RETRY_BACKOFF_MS);
}

static bool gh_apply_request_legacy_execute(uint8_t slave_id, const gh_slave_apply_request_t *req)
{
  bool apply_ok;

  if (req == NULL)
  {
    return false;
  }

  /* Mapping from master TCP setpoints to current slave control map. */
  apply_ok = gh_modbus_write_single_retry(slave_id,
                                          GH_RTU_CTRL_MODE_CMD_REG,
                                          req->setpoints[0],
                                          MODBUS_RETRY_COUNT,
                                          MODBUS_RTU_RESP_TIMEOUT_MS);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_CTRL_SP_WATER_RAIL_REG,
                                                       req->setpoints[1],
                                                       MODBUS_RETRY_COUNT,
                                                       MODBUS_RTU_RESP_TIMEOUT_MS);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_CTRL_SP_WATER_GROW_REG,
                                                       req->setpoints[2],
                                                       MODBUS_RETRY_COUNT,
                                                       MODBUS_RTU_RESP_TIMEOUT_MS);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_CTRL_SP_WATER_UPPER_REG,
                                                       req->setpoints[3],
                                                       MODBUS_RETRY_COUNT,
                                                       MODBUS_RTU_RESP_TIMEOUT_MS);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_CTRL_SP_WATER_UNDER_REG,
                                                       req->setpoints[4],
                                                       MODBUS_RETRY_COUNT,
                                                       MODBUS_RTU_RESP_TIMEOUT_MS);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_APPLY_TRIGGER_REG,
                                                       req->trigger,
                                                       MODBUS_RETRY_COUNT,
                                                       MODBUS_RTU_RESP_TIMEOUT_MS);

  return apply_ok;
}

static bool gh_apply_request_topology_execute(uint8_t slave_id,
                                              const gh_slave_apply_request_t *req,
                                              const gh_topology_cmd_binding_t *cmds,
                                              uint16_t cmd_count,
                                              const gh_topology_point_binding_t *points,
                                              uint16_t point_count,
                                              bool *out_used_topology)
{
  uint16_t payload[GH_MB_CMD_PAYLOAD_WORDS];
  uint16_t i;
  bool apply_ok = true;
  bool used = false;
  bool cmd_ok;
  uint32_t timeout_ms;
  uint16_t reg_count;

  if ((req == NULL) || (out_used_topology == NULL))
  {
    return false;
  }

  payload[0] = req->setpoints[0];
  payload[1] = req->setpoints[1];
  payload[2] = req->setpoints[2];
  payload[3] = req->setpoints[3];
  payload[4] = req->setpoints[4];
  payload[5] = req->setpoints[5];
  payload[6] = req->setpoints[6];
  payload[7] = req->out_cmd_mask;

  for (i = 0U; i < cmd_count; i++)
  {
    if ((cmds[i].slave_id != slave_id) || (cmds[i].module_id == 0U))
    {
      continue;
    }

    used = true;
    timeout_ms = (cmds[i].timeout_ms == 0U) ? MODBUS_RTU_RESP_TIMEOUT_MS : cmds[i].timeout_ms;
    cmd_ok = false;

    if (cmds[i].fc == GH_CMD_FC_WRITE_SINGLE)
    {
      cmd_ok = gh_modbus_write_single_retry(slave_id,
                                            cmds[i].start_reg,
                                            payload[0],
                                            cmds[i].retries,
                                            timeout_ms);
    }
    else if (cmds[i].fc == GH_CMD_FC_WRITE_MULTI)
    {
      reg_count = cmds[i].reg_count;
      if ((reg_count > 0U) && (reg_count <= GH_MB_CMD_PAYLOAD_WORDS))
      {
        cmd_ok = gh_modbus_write_multi_retry(slave_id,
                                             cmds[i].start_reg,
                                             reg_count,
                                             payload,
                                             cmds[i].retries,
                                             timeout_ms);
      }
    }

    if (cmd_ok && (cmds[i].ack_point_id != 0U))
    {
      cmd_ok = gh_command_ack_probe(slave_id, &cmds[i], points, point_count);
    }

    apply_ok = apply_ok && cmd_ok;
  }

  *out_used_topology = used;
  return apply_ok;
}

static void gh_apply_request_for_slave(uint8_t slave_id,
                                       const gh_topology_cmd_binding_t *cmds,
                                       uint16_t cmd_count,
                                       const gh_topology_point_binding_t *points,
                                       uint16_t point_count)
{
  bool apply_ok = false;
  bool used_topology = false;
  gh_slave_apply_request_t req = {0};

  if (!GH_ModbusMap_GetApplyRequest(slave_id, &req))
  {
    return;
  }

  if ((cmds != NULL) && (cmd_count > 0U))
  {
    apply_ok = gh_apply_request_topology_execute(slave_id,
                                                 &req,
                                                 cmds,
                                                 cmd_count,
                                                 points,
                                                 point_count,
                                                 &used_topology);
  }

  if (!used_topology)
  {
    apply_ok = gh_apply_request_legacy_execute(slave_id, &req);
  }

  GH_ModbusMap_MarkApplyResult(slave_id, req.trigger, apply_ok);
  if (!apply_ok)
  {
    g_status.last_error_code = EVENT_CODE_CTRL_SYNC_FAIL;
  }
}

static void gh_apply_requests_for_mask(uint32_t slave_mask,
                                       const gh_topology_cmd_binding_t *cmds,
                                       uint16_t cmd_count,
                                       const gh_topology_point_binding_t *points,
                                       uint16_t point_count)
{
  uint8_t slave_id;
  uint32_t bit;

  for (slave_id = GH_RTU_SLAVE_FIRST; slave_id <= GH_RTU_SLAVE_LAST; slave_id++)
  {
    bit = (1UL << (uint32_t)(slave_id - 1U));
    if ((slave_mask & bit) != 0U)
    {
      gh_apply_request_for_slave(slave_id, cmds, cmd_count, points, point_count);
    }
  }
}

static bool gh_run_topology_cycle(uint8_t *comm_fail_streak,
                                  uint8_t *comm_ok_streak,
                                  uint8_t fail_offline_cycles)
{
  bool slave_attempted[MODBUS_MAX_SLAVES] = {false};
  bool slave_success[MODBUS_MAX_SLAVES] = {false};
  uint16_t regs[MODBUS_MAX_REGS_PER_REQ];
  uint16_t sens[GH_RTU_SENSORS_PER_SLAVE];
  static uint16_t s_last_sens[MODBUS_MAX_SLAVES][GH_RTU_SENSORS_PER_SLAVE] = {{0U}};
  static uint32_t s_next_due_ms[GH_TOPOLOGY_V2_MAX_REQ_PROFILES] = {0U};
  static uint32_t s_last_plan_generation = 0U;
  static uint16_t s_last_plan_count = 0U;
  uint16_t plan_count = 0U;
  uint16_t point_count = 0U;
  uint16_t cmd_count = 0U;
  uint32_t plan_generation = 0U;
  uint32_t point_generation = 0U;
  uint32_t cmd_generation = 0U;
  uint32_t plan_slave_mask = 0U;
  uint16_t i;
  uint16_t w;
  uint16_t valid_mask = 0U;
  uint32_t now_ms = HAL_GetTick();
  uint8_t slave_id;
  uint8_t slave_idx;
  bool ok;
  uint8_t quality;

  if (!GH_TopologyRuntime_CopyPollPlan(s_topology_plan_cache,
                                       GH_TOPOLOGY_V2_MAX_REQ_PROFILES,
                                       &plan_count,
                                       &plan_generation,
                                       &plan_slave_mask))
  {
    return false;
  }
  if (!GH_TopologyRuntime_CopyPointBindings(s_topology_point_cache,
                                            GH_TOPOLOGY_V2_MAX_POINTS,
                                            &point_count,
                                            &point_generation))
  {
    return false;
  }
  if (!GH_TopologyRuntime_CopyCommandBindings(s_topology_cmd_cache,
                                              GH_TOPOLOGY_V2_MAX_COMMANDS,
                                              &cmd_count,
                                              &cmd_generation))
  {
    return false;
  }
  if ((point_generation != plan_generation) || (cmd_generation != plan_generation))
  {
    if (!GH_TopologyRuntime_CopyPollPlan(s_topology_plan_cache,
                                         GH_TOPOLOGY_V2_MAX_REQ_PROFILES,
                                         &plan_count,
                                         &plan_generation,
                                         &plan_slave_mask))
    {
      return false;
    }
    if (!GH_TopologyRuntime_CopyPointBindings(s_topology_point_cache,
                                              GH_TOPOLOGY_V2_MAX_POINTS,
                                              &point_count,
                                              &point_generation))
    {
      return false;
    }
    if (!GH_TopologyRuntime_CopyCommandBindings(s_topology_cmd_cache,
                                                GH_TOPOLOGY_V2_MAX_COMMANDS,
                                                &cmd_count,
                                                &cmd_generation))
    {
      return false;
    }
    if ((plan_count == 0U) ||
        (point_generation != plan_generation) ||
        (cmd_generation != plan_generation))
    {
      return false;
    }
  }

  if (plan_count == 0U)
  {
    return false;
  }

  if ((plan_generation != s_last_plan_generation) || (plan_count != s_last_plan_count))
  {
    memset(s_next_due_ms, 0, sizeof(s_next_due_ms));
    s_last_plan_generation = plan_generation;
    s_last_plan_count = plan_count;
  }

  for (i = 0U; i < plan_count; i++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    now_ms = HAL_GetTick();

    if ((s_next_due_ms[i] != 0U) && ((int32_t)(now_ms - s_next_due_ms[i]) < 0))
    {
      continue;
    }

    slave_id = s_topology_plan_cache[i].slave_id;
    if (!gh_slave_id_valid(slave_id))
    {
      continue;
    }
    slave_idx = gh_slave_to_index(slave_id);
    slave_attempted[slave_idx] = true;

    ok = gh_modbus_read_holding_retry(slave_id,
                                      s_topology_plan_cache[i].start_reg,
                                      s_topology_plan_cache[i].reg_count,
                                      regs,
                                      s_topology_plan_cache[i].timeout_ms,
                                      s_topology_plan_cache[i].retries,
                                      s_topology_plan_cache[i].backoff_ms);
    s_next_due_ms[i] = now_ms + s_topology_plan_cache[i].period_ms;

    if (!ok)
    {
      osDelay(MODBUS_INTER_SLAVE_DELAY_MS);
      continue;
    }

    slave_success[slave_idx] = true;

    if (s_topology_plan_cache[i].telemetry_word_count > 0U)
    {
      valid_mask = 0U;
      memset(sens, 0, sizeof(sens));

      for (w = 0U; w < s_topology_plan_cache[i].telemetry_word_count; w++)
      {
        valid_mask |= (uint16_t)(1U << w);
        s_last_sens[slave_idx][w] = regs[w];
      }
      for (w = 0U; w < GH_RTU_SENSORS_PER_SLAVE; w++)
      {
        sens[w] = s_last_sens[slave_idx][w];
      }

      GH_ModbusMap_UpdateTelemetry(slave_id, sens, valid_mask, 0U, now_ms);
    }
    gh_publish_points_from_request(&s_topology_plan_cache[i], regs, now_ms, s_topology_point_cache, point_count);

    if ((s_topology_plan_cache[i].diag_offset != GH_TOPOLOGY_DIAG_OFFSET_NONE) &&
        ((uint16_t)s_topology_plan_cache[i].diag_offset + GH_RTU_DIAG_COUNT <= s_topology_plan_cache[i].reg_count))
    {
      uint8_t off = s_topology_plan_cache[i].diag_offset;
      GH_ModbusMap_UpdateDiag(slave_id, regs[off + 0U], regs[off + 1U], regs[off + 2U]);
      g_status.control_mode = (uint8_t)(regs[off + 0U] & 0x00FFU);
      g_status.autonomous_reason = (uint8_t)(regs[off + 1U] & 0x00FFU);
      g_status.last_master_seen_ms = ((uint32_t)regs[off + 3U] << 16U) | (uint32_t)regs[off + 2U];
      g_status.good_cycle_streak = regs[off + 4U];
      g_status.last_apply_status = (uint8_t)(regs[off + 5U] & 0x00FFU);
    }

    osDelay(MODBUS_INTER_SLAVE_DELAY_MS);
  }

  for (slave_id = GH_RTU_SLAVE_FIRST; slave_id <= GH_RTU_SLAVE_LAST; slave_id++)
  {
    uint32_t bit = (1UL << (uint32_t)(slave_id - 1U));

    if ((plan_slave_mask & bit) == 0U)
    {
      continue;
    }

    slave_idx = gh_slave_to_index(slave_id);
    if (!slave_attempted[slave_idx])
    {
      continue;
    }

    if (slave_success[slave_idx])
    {
      comm_fail_streak[slave_idx] = 0U;
      if (comm_ok_streak[slave_idx] < 0xFFU)
      {
        comm_ok_streak[slave_idx]++;
      }
      quality = (comm_ok_streak[slave_idx] >= GH_QUALITY_RECOVER_OK_CYCLES) ?
                  SENSOR_QUALITY_OK :
                  SENSOR_QUALITY_STALE;
      if (point_count > 0U)
      {
        gh_set_points_quality_for_slave(slave_id, quality, s_topology_point_cache, point_count);
      }
      else
      {
        gh_set_slave_quality(slave_id, quality);
      }
    }
    else
    {
      comm_ok_streak[slave_idx] = 0U;
      if (comm_fail_streak[slave_idx] < 0xFFU)
      {
        comm_fail_streak[slave_idx]++;
      }

      if (comm_fail_streak[slave_idx] >= fail_offline_cycles)
      {
        if (point_count > 0U)
        {
          gh_set_points_quality_for_slave(slave_id, SENSOR_QUALITY_OFFLINE, s_topology_point_cache, point_count);
        }
        else
        {
          gh_set_slave_quality(slave_id, SENSOR_QUALITY_OFFLINE);
        }
      }
      else if (comm_fail_streak[slave_idx] >= GH_QUALITY_FAIL_STALE_CYCLES)
      {
        if (point_count > 0U)
        {
          gh_set_points_quality_for_slave(slave_id, SENSOR_QUALITY_STALE, s_topology_point_cache, point_count);
        }
        else
        {
          gh_set_slave_quality(slave_id, SENSOR_QUALITY_STALE);
        }
      }

      if (slave_id <= (sizeof(g_status.modbus_timeouts) / sizeof(g_status.modbus_timeouts[0])))
      {
        g_status.modbus_timeouts[slave_id - 1U]++;
      }
      GH_ModbusMap_ReportTimeout(slave_id, HAL_GetTick());
    }
  }

  gh_apply_requests_for_mask(plan_slave_mask,
                             s_topology_cmd_cache,
                             cmd_count,
                             s_topology_point_cache,
                             point_count);
  return true;
}

static void gh_run_legacy_cycle(uint8_t *comm_fail_streak,
                                uint8_t *comm_ok_streak,
                                uint8_t fail_offline_cycles)
{
  uint16_t regs[GH_RTU_READ_REG_COUNT];
  uint16_t diag_regs[GH_RTU_DIAG_COUNT];
  uint16_t sens[GH_RTU_SENSORS_PER_SLAVE];
  uint16_t i;
  uint16_t s;
  uint16_t global_idx;
  uint32_t now = 0U;
  bool ok;
  bool set_quality = false;
  uint8_t target_quality = SENSOR_QUALITY_STALE;
  uint8_t slave_id;
  uint8_t streak_idx = 0U;

  for (s = GH_RTU_SLAVE_FIRST; s <= GH_RTU_SLAVE_LAST; s++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    slave_id = (uint8_t)s;
    now = HAL_GetTick();
    streak_idx = (uint8_t)(slave_id - 1U);
    set_quality = false;
    target_quality = SENSOR_QUALITY_STALE;

    if (!gh_slave_enabled(slave_id))
    {
      osDelay(MODBUS_INTER_SLAVE_DELAY_MS);
      task_heartbeat_kick(TASK_BIT_MODBUS);
      continue;
    }

    ok = gh_modbus_read_holding_retry(slave_id,
                                      GH_RTU_READ_START_REG,
                                      GH_RTU_READ_REG_COUNT,
                                      regs,
                                      MODBUS_RTU_RESP_TIMEOUT_MS,
                                      MODBUS_RETRY_COUNT,
                                      MODBUS_RETRY_BACKOFF_MS);
    if (ok)
    {
      comm_fail_streak[streak_idx] = 0U;
      if (comm_ok_streak[streak_idx] < 0xFFU)
      {
        comm_ok_streak[streak_idx]++;
      }

      if (comm_ok_streak[streak_idx] >= GH_QUALITY_RECOVER_OK_CYCLES)
      {
        set_quality = true;
        target_quality = SENSOR_QUALITY_OK;
      }
      else
      {
        set_quality = true;
        target_quality = SENSOR_QUALITY_STALE;
      }

      for (i = 0U; i < GH_RTU_SENSORS_PER_SLAVE; i++)
      {
        sens[i] = regs[i];
        global_idx = (uint16_t)(((s - 1U) * GH_RTU_SENSORS_PER_SLAVE) + i);
        if (global_idx < SENSOR_COUNT)
        {
          g_sensors[global_idx].value = ((float)(int16_t)sens[i]) / 10.0f;
          if (set_quality)
          {
            g_sensors[global_idx].quality = target_quality;
          }
          g_sensors[global_idx].timestamp_ms = now;
        }
      }
      GH_ModbusMap_UpdateTelemetry(slave_id, sens, 0x01FFU, 0U, now);
    }
    else
    {
      comm_ok_streak[streak_idx] = 0U;
      if (comm_fail_streak[streak_idx] < 0xFFU)
      {
        comm_fail_streak[streak_idx]++;
      }

      if (comm_fail_streak[streak_idx] >= fail_offline_cycles)
      {
        set_quality = true;
        target_quality = SENSOR_QUALITY_OFFLINE;
      }
      else if (comm_fail_streak[streak_idx] >= GH_QUALITY_FAIL_STALE_CYCLES)
      {
        set_quality = true;
        target_quality = SENSOR_QUALITY_STALE;
      }

      if (s <= (sizeof(g_status.modbus_timeouts) / sizeof(g_status.modbus_timeouts[0])))
      {
        g_status.modbus_timeouts[s - 1U]++;
      }
      GH_ModbusMap_ReportTimeout(slave_id, now);
      if (set_quality)
      {
        for (i = 0U; i < GH_RTU_SENSORS_PER_SLAVE; i++)
        {
          global_idx = (uint16_t)(((s - 1U) * GH_RTU_SENSORS_PER_SLAVE) + i);
          if (global_idx < SENSOR_COUNT)
          {
            g_sensors[global_idx].quality = target_quality;
          }
        }
      }
    }

    if (ok &&
        gh_modbus_read_holding_retry(slave_id,
                                     GH_RTU_DIAG_BASE_REG,
                                     GH_RTU_DIAG_COUNT,
                                     diag_regs,
                                     MODBUS_RTU_RESP_TIMEOUT_MS,
                                     MODBUS_RETRY_COUNT,
                                     MODBUS_RETRY_BACKOFF_MS))
    {
      GH_ModbusMap_UpdateDiag(slave_id, diag_regs[0], diag_regs[1], diag_regs[2]);
      g_status.control_mode = (uint8_t)(diag_regs[0] & 0x00FFU);
      g_status.autonomous_reason = (uint8_t)(diag_regs[1] & 0x00FFU);
      g_status.last_master_seen_ms = ((uint32_t)diag_regs[3] << 16U) | (uint32_t)diag_regs[2];
      g_status.good_cycle_streak = diag_regs[4];
      g_status.last_apply_status = (uint8_t)(diag_regs[5] & 0x00FFU);
    }

    gh_apply_request_for_slave(slave_id, NULL, 0U, NULL, 0U);

    osDelay(MODBUS_INTER_SLAVE_DELAY_MS);
    task_heartbeat_kick(TASK_BIT_MODBUS);
  }
}

void GH_ModbusMasterTask_Run(void *argument)
{
  static uint8_t s_comm_fail_streak[MODBUS_MAX_SLAVES] = {0};
  static uint8_t s_comm_ok_streak[MODBUS_MAX_SLAVES] = {0};
  uint32_t cycle_start_ms = 0U;
  uint32_t cycle_elapsed_ms = 0U;
  uint8_t fail_offline_cycles = 0U;
  bool topology_mode = false;
  (void)argument;

  fail_offline_cycles = gh_quality_fail_offline_cycles();

  for (;;)
  {
    cycle_start_ms = HAL_GetTick();
    task_heartbeat_kick(TASK_BIT_MODBUS);
    GH_ModbusMap_UpdateAges(cycle_start_ms);

    topology_mode = gh_run_topology_cycle(s_comm_fail_streak, s_comm_ok_streak, fail_offline_cycles);
    if (!topology_mode)
    {
      gh_run_legacy_cycle(s_comm_fail_streak, s_comm_ok_streak, fail_offline_cycles);
    }

    task_heartbeat_kick(TASK_BIT_MODBUS);
    cycle_elapsed_ms = HAL_GetTick() - cycle_start_ms;
    if (cycle_elapsed_ms < MODBUS_POLL_PERIOD_MS)
    {
      osDelay(MODBUS_POLL_PERIOD_MS - cycle_elapsed_ms);
    }
    else
    {
      osDelay(MODBUS_INTER_SLAVE_DELAY_MS);
    }
  }
}
