#include "gh_modbus_master.h"

#include "gh_runtime_state.h"
#include "gh_modbus_map.h"

#define GH_RTU_SLAVE_FIRST            1U
#define GH_RTU_SLAVE_LAST             20U
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

void GH_ModbusMasterTask_Run(void *argument)
{
  uint16_t regs[GH_RTU_READ_REG_COUNT];
  uint16_t diag_regs[GH_RTU_DIAG_COUNT];
  uint16_t sens[GH_RTU_SENSORS_PER_SLAVE];
  uint16_t i;
  uint16_t s;
  uint16_t global_idx;
  uint32_t now = 0U;
  bool ok;
  bool apply_ok;
  gh_slave_apply_request_t req = {0};
  (void)argument;

  for (;;)
  {
    now = HAL_GetTick();
    GH_ModbusMap_UpdateAges(now);

    for (s = GH_RTU_SLAVE_FIRST; s <= GH_RTU_SLAVE_LAST; s++)
    {
      ok = modbus_read_holding_registers((uint8_t)s,
                                         GH_RTU_READ_START_REG,
                                         GH_RTU_READ_REG_COUNT,
                                         regs);
      if (ok)
      {
        for (i = 0U; i < GH_RTU_SENSORS_PER_SLAVE; i++)
        {
          sens[i] = regs[i];
          global_idx = (uint16_t)(((s - 1U) * GH_RTU_SENSORS_PER_SLAVE) + i);
          if (global_idx < SENSOR_COUNT)
          {
            g_sensors[global_idx].value = ((float)(int16_t)sens[i]) / 10.0f;
            g_sensors[global_idx].quality = SENSOR_QUALITY_OK;
            g_sensors[global_idx].timestamp_ms = now;
          }
        }
        GH_ModbusMap_UpdateTelemetry((uint8_t)s, sens, 0x01FFU, 0U, now);
      }
      else
      {
        if (s <= (sizeof(g_status.modbus_timeouts) / sizeof(g_status.modbus_timeouts[0])))
        {
          g_status.modbus_timeouts[s - 1U]++;
        }
        GH_ModbusMap_ReportTimeout((uint8_t)s, now);
        for (i = 0U; i < GH_RTU_SENSORS_PER_SLAVE; i++)
        {
          global_idx = (uint16_t)(((s - 1U) * GH_RTU_SENSORS_PER_SLAVE) + i);
          if (global_idx < SENSOR_COUNT)
          {
            g_sensors[global_idx].quality = SENSOR_QUALITY_STALE;
          }
        }
      }

      if (modbus_read_holding_registers((uint8_t)s, GH_RTU_DIAG_BASE_REG, GH_RTU_DIAG_COUNT, diag_regs))
      {
        GH_ModbusMap_UpdateDiag((uint8_t)s, diag_regs[0], diag_regs[1], diag_regs[2]);
        g_status.control_mode = (uint8_t)(diag_regs[0] & 0x00FFU);
        g_status.autonomous_reason = (uint8_t)(diag_regs[1] & 0x00FFU);
        g_status.last_master_seen_ms = ((uint32_t)diag_regs[3] << 16U) | (uint32_t)diag_regs[2];
        g_status.good_cycle_streak = diag_regs[4];
        g_status.last_apply_status = (uint8_t)(diag_regs[5] & 0x00FFU);
      }

      if (GH_ModbusMap_GetApplyRequest((uint8_t)s, &req))
      {
        /* Mapping from master TCP setpoints to current slave control map. */
        apply_ok = modbus_write_single_holding_register((uint8_t)s, GH_RTU_CTRL_MODE_CMD_REG, req.setpoints[0]);
        apply_ok = apply_ok && modbus_write_single_holding_register((uint8_t)s,
                                                                     GH_RTU_CTRL_SP_WATER_RAIL_REG,
                                                                     req.setpoints[1]);
        apply_ok = apply_ok && modbus_write_single_holding_register((uint8_t)s,
                                                                     GH_RTU_CTRL_SP_WATER_GROW_REG,
                                                                     req.setpoints[2]);
        apply_ok = apply_ok && modbus_write_single_holding_register((uint8_t)s,
                                                                     GH_RTU_CTRL_SP_WATER_UPPER_REG,
                                                                     req.setpoints[3]);
        apply_ok = apply_ok && modbus_write_single_holding_register((uint8_t)s,
                                                                     GH_RTU_CTRL_SP_WATER_UNDER_REG,
                                                                     req.setpoints[4]);
        apply_ok = apply_ok && modbus_write_single_holding_register((uint8_t)s,
                                                                    GH_RTU_APPLY_TRIGGER_REG,
                                                                    req.trigger);
        GH_ModbusMap_MarkApplyResult((uint8_t)s, req.trigger, apply_ok);
        if (!apply_ok)
        {
          g_status.last_error_code = EVENT_CODE_CTRL_SYNC_FAIL;
        }
      }

      osDelay(1U);
    }

    task_heartbeat_kick(TASK_BIT_MODBUS);
    osDelay(1U);
  }
}
