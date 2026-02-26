#include "gh_modbus_master.h"

#include "gh_runtime_state.h"
#include "gh_modbus_map.h"
#include "gh_topology_runtime.h"
#include "gh_topology_v2.h"

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
                                         uint8_t retries,
                                         uint8_t backoff_ms)
{
  uint8_t attempt;
  uint8_t retry_count;

  retry_count = (retries == 0U) ? 1U : retries;

  for (attempt = 0U; attempt < retry_count; attempt++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if (modbus_read_holding_registers(slave_id, start_reg, reg_count, out_regs))
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

static bool gh_modbus_write_single_retry(uint8_t slave_id, uint16_t reg, uint16_t value)
{
  uint8_t attempt;

  for (attempt = 0U; attempt < MODBUS_RETRY_COUNT; attempt++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if (modbus_write_single_holding_register(slave_id, reg, value))
    {
      task_heartbeat_kick(TASK_BIT_MODBUS);
      return true;
    }
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if ((attempt + 1U) < MODBUS_RETRY_COUNT)
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

static void gh_apply_request_for_slave(uint8_t slave_id)
{
  bool apply_ok;
  gh_slave_apply_request_t req = {0};

  if (!GH_ModbusMap_GetApplyRequest(slave_id, &req))
  {
    return;
  }

  /* Mapping from master TCP setpoints to current slave control map. */
  apply_ok = gh_modbus_write_single_retry(slave_id, GH_RTU_CTRL_MODE_CMD_REG, req.setpoints[0]);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_CTRL_SP_WATER_RAIL_REG,
                                                       req.setpoints[1]);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_CTRL_SP_WATER_GROW_REG,
                                                       req.setpoints[2]);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_CTRL_SP_WATER_UPPER_REG,
                                                       req.setpoints[3]);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_CTRL_SP_WATER_UNDER_REG,
                                                       req.setpoints[4]);
  apply_ok = apply_ok && gh_modbus_write_single_retry(slave_id,
                                                       GH_RTU_APPLY_TRIGGER_REG,
                                                       req.trigger);
  GH_ModbusMap_MarkApplyResult(slave_id, req.trigger, apply_ok);
  if (!apply_ok)
  {
    g_status.last_error_code = EVENT_CODE_CTRL_SYNC_FAIL;
  }
}

static void gh_apply_requests_for_mask(uint32_t slave_mask)
{
  uint8_t slave_id;
  uint32_t bit;

  for (slave_id = GH_RTU_SLAVE_FIRST; slave_id <= GH_RTU_SLAVE_LAST; slave_id++)
  {
    bit = (1UL << (uint32_t)(slave_id - 1U));
    if ((slave_mask & bit) != 0U)
    {
      gh_apply_request_for_slave(slave_id);
    }
  }
}

static bool gh_run_topology_cycle(uint8_t *comm_fail_streak,
                                  uint8_t *comm_ok_streak,
                                  uint8_t fail_offline_cycles)
{
  gh_topology_poll_req_t plan[GH_TOPOLOGY_V2_MAX_REQ_PROFILES];
  bool slave_attempted[MODBUS_MAX_SLAVES] = {false};
  bool slave_success[MODBUS_MAX_SLAVES] = {false};
  uint16_t regs[MODBUS_MAX_REGS_PER_REQ];
  uint16_t sens[GH_RTU_SENSORS_PER_SLAVE];
  static uint16_t s_last_sens[MODBUS_MAX_SLAVES][GH_RTU_SENSORS_PER_SLAVE] = {{0U}};
  static uint32_t s_next_due_ms[GH_TOPOLOGY_V2_MAX_REQ_PROFILES] = {0U};
  static uint32_t s_last_plan_generation = 0U;
  static uint16_t s_last_plan_count = 0U;
  uint16_t plan_count = 0U;
  uint32_t plan_generation = 0U;
  uint32_t plan_slave_mask = 0U;
  uint16_t i;
  uint16_t w;
  uint16_t valid_mask = 0U;
  uint16_t global_idx;
  uint32_t now_ms = HAL_GetTick();
  uint8_t slave_id;
  uint8_t slave_idx;
  bool ok;
  uint8_t quality;

  if (!GH_TopologyRuntime_CopyPollPlan(plan,
                                       GH_TOPOLOGY_V2_MAX_REQ_PROFILES,
                                       &plan_count,
                                       &plan_generation,
                                       &plan_slave_mask))
  {
    return false;
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

    slave_id = plan[i].slave_id;
    if (!gh_slave_id_valid(slave_id))
    {
      continue;
    }
    slave_idx = gh_slave_to_index(slave_id);
    slave_attempted[slave_idx] = true;

    ok = gh_modbus_read_holding_retry(slave_id,
                                      plan[i].start_reg,
                                      plan[i].reg_count,
                                      regs,
                                      plan[i].retries,
                                      plan[i].backoff_ms);
    s_next_due_ms[i] = now_ms + plan[i].period_ms;

    if (!ok)
    {
      osDelay(MODBUS_INTER_SLAVE_DELAY_MS);
      continue;
    }

    slave_success[slave_idx] = true;

    if (plan[i].telemetry_word_count > 0U)
    {
      valid_mask = 0U;
      memset(sens, 0, sizeof(sens));

      for (w = 0U; w < plan[i].telemetry_word_count; w++)
      {
        valid_mask |= (uint16_t)(1U << w);
        s_last_sens[slave_idx][w] = regs[w];
      }
      for (w = 0U; w < GH_RTU_SENSORS_PER_SLAVE; w++)
      {
        sens[w] = s_last_sens[slave_idx][w];
      }

      GH_ModbusMap_UpdateTelemetry(slave_id, sens, valid_mask, 0U, now_ms);

      for (w = 0U; w < plan[i].telemetry_word_count; w++)
      {
        global_idx = (uint16_t)(((uint16_t)slave_idx * GH_RTU_SENSORS_PER_SLAVE) + w);
        if (global_idx < SENSOR_COUNT)
        {
          g_sensors[global_idx].value = ((float)(int16_t)regs[w]) / 10.0f;
          g_sensors[global_idx].timestamp_ms = now_ms;
        }
      }
    }

    if ((plan[i].diag_offset != GH_TOPOLOGY_DIAG_OFFSET_NONE) &&
        ((uint16_t)plan[i].diag_offset + GH_RTU_DIAG_COUNT <= plan[i].reg_count))
    {
      uint8_t off = plan[i].diag_offset;
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
      gh_set_slave_quality(slave_id, quality);
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
        gh_set_slave_quality(slave_id, SENSOR_QUALITY_OFFLINE);
      }
      else if (comm_fail_streak[slave_idx] >= GH_QUALITY_FAIL_STALE_CYCLES)
      {
        gh_set_slave_quality(slave_id, SENSOR_QUALITY_STALE);
      }

      if (slave_id <= (sizeof(g_status.modbus_timeouts) / sizeof(g_status.modbus_timeouts[0])))
      {
        g_status.modbus_timeouts[slave_id - 1U]++;
      }
      GH_ModbusMap_ReportTimeout(slave_id, HAL_GetTick());
    }
  }

  gh_apply_requests_for_mask(plan_slave_mask);
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

    gh_apply_request_for_slave(slave_id);

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
