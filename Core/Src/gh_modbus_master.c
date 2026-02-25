#include "gh_modbus_master.h"

#include "gh_runtime_state.h"
#include "gh_modbus_map.h"

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
                                         uint16_t *out_regs)
{
  uint8_t attempt;

  for (attempt = 0U; attempt < MODBUS_RETRY_COUNT; attempt++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if (modbus_read_holding_registers(slave_id, start_reg, reg_count, out_regs))
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

void GH_ModbusMasterTask_Run(void *argument)
{
  uint16_t regs[GH_RTU_READ_REG_COUNT];
  uint16_t diag_regs[GH_RTU_DIAG_COUNT];
  uint16_t sens[GH_RTU_SENSORS_PER_SLAVE];
  static uint8_t s_comm_fail_streak[MODBUS_MAX_SLAVES] = {0};
  static uint8_t s_comm_ok_streak[MODBUS_MAX_SLAVES] = {0};
  uint16_t i;
  uint16_t s;
  uint16_t global_idx;
  uint32_t cycle_start_ms = 0U;
  uint32_t cycle_elapsed_ms = 0U;
  uint32_t now = 0U;
  bool ok;
  bool apply_ok;
  bool set_quality = false;
  uint8_t target_quality = SENSOR_QUALITY_STALE;
  uint8_t slave_id;
  uint8_t streak_idx = 0U;
  uint8_t fail_offline_cycles = 0U;
  gh_slave_apply_request_t req = {0};
  (void)argument;

  fail_offline_cycles = gh_quality_fail_offline_cycles();

  for (;;)
  {
    cycle_start_ms = HAL_GetTick();
    task_heartbeat_kick(TASK_BIT_MODBUS);
    GH_ModbusMap_UpdateAges(cycle_start_ms);

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
                                        regs);
      if (ok)
      {
        s_comm_fail_streak[streak_idx] = 0U;
        if (s_comm_ok_streak[streak_idx] < 0xFFU)
        {
          s_comm_ok_streak[streak_idx]++;
        }

        if (s_comm_ok_streak[streak_idx] >= GH_QUALITY_RECOVER_OK_CYCLES)
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
        s_comm_ok_streak[streak_idx] = 0U;
        if (s_comm_fail_streak[streak_idx] < 0xFFU)
        {
          s_comm_fail_streak[streak_idx]++;
        }

        if (s_comm_fail_streak[streak_idx] >= fail_offline_cycles)
        {
          set_quality = true;
          target_quality = SENSOR_QUALITY_OFFLINE;
        }
        else if (s_comm_fail_streak[streak_idx] >= GH_QUALITY_FAIL_STALE_CYCLES)
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
          gh_modbus_read_holding_retry(slave_id, GH_RTU_DIAG_BASE_REG, GH_RTU_DIAG_COUNT, diag_regs))
      {
        GH_ModbusMap_UpdateDiag(slave_id, diag_regs[0], diag_regs[1], diag_regs[2]);
        g_status.control_mode = (uint8_t)(diag_regs[0] & 0x00FFU);
        g_status.autonomous_reason = (uint8_t)(diag_regs[1] & 0x00FFU);
        g_status.last_master_seen_ms = ((uint32_t)diag_regs[3] << 16U) | (uint32_t)diag_regs[2];
        g_status.good_cycle_streak = diag_regs[4];
        g_status.last_apply_status = (uint8_t)(diag_regs[5] & 0x00FFU);
      }

      if (GH_ModbusMap_GetApplyRequest(slave_id, &req))
      {
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

      osDelay(MODBUS_INTER_SLAVE_DELAY_MS);
      task_heartbeat_kick(TASK_BIT_MODBUS);
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
