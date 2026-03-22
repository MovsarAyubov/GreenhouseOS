#include "gh_modbus_master.h"

#include "gh_runtime_state.h"
#include "gh_modbus_map.h"
#include "gh_topology_runtime.h"
#include "gh_topology_v2.h"
#include "ModbusConfig.h"

#include <math.h>
#include <string.h>

#define GH_RTU_SLAVE_FIRST            1U
#define GH_RTU_SLAVE_LAST             MODBUS_MAX_SLAVES
#define GH_RTU_SENSORS_PER_SLAVE      9U
#define GH_RTU_DIAG_COUNT             6U
#define GH_RTU_APPLY_TRIGGER_REG      122U
#define GH_RTU_APPLY_STATUS_REG       125U
#define GH_RTU_APPLY_STATUS_REG_COUNT 3U
#define GH_SCHEDULE_SLOT_WORDS        3U
#define GH_SCHEDULE_SLOT_COUNT        4U
#define GH_SCHEDULE_PAYLOAD_REG_COUNT (GH_SCHEDULE_SLOT_WORDS * GH_SCHEDULE_SLOT_COUNT)
#define GH_SCHEDULE_PAYLOAD_OFF_APPLY_VALUE      GH_SCHEDULE_PAYLOAD_REG_COUNT
#define GH_SCHEDULE_PAYLOAD_OFF_EXPECTED_VER_HI  (GH_SCHEDULE_PAYLOAD_REG_COUNT + 1U)
#define GH_SCHEDULE_PAYLOAD_OFF_EXPECTED_VER_LO  (GH_SCHEDULE_PAYLOAD_REG_COUNT + 2U)
#define GH_SCHEDULE_PAYLOAD_OFF_CMD_KIND         (GH_SCHEDULE_PAYLOAD_REG_COUNT + 3U)
#define GH_SCHEDULE_PAYLOAD_REQUIRED_WORDS       (GH_SCHEDULE_PAYLOAD_OFF_CMD_KIND + 1U)
#define GH_RTU_RTC_SET_BASE_REG       140U
#define GH_RTU_RTC_SET_HOUR_REG       (GH_RTU_RTC_SET_BASE_REG + 0U)
#define GH_RTU_RTC_SET_MINUTE_REG     (GH_RTU_RTC_SET_BASE_REG + 1U)
#define GH_RTU_RTC_APPLIED_TOKEN_REG  (GH_RTU_RTC_SET_BASE_REG + 3U)
#define GH_RTU_RTC_SYNC_RESULT_APPLIED      2U
#define GH_RTU_RTC_SYNC_RESULT_NOOP         5U
#define GH_RTU_RTC_SYNC_PERIOD_MS           86400000UL
#define GH_RTU_RTC_SYNC_RETRY_BACKOFF_MS    60000UL
#define GH_RTU_RTC_SYNC_MAX_RETRIES         3U
#define GH_RTU_RTC_SYNC_ACK_POLL_ATTEMPTS   3U
#define GH_RTU_RTC_SYNC_ACK_POLL_DELAY_MS   100U
#define GH_MODULE_ID_ZONE_FIRST             100U
#define GH_MODULE_ID_ZONE_LAST              199U
#define GH_MODULE_ID_WEATHER_FIRST          200U
#define GH_MODULE_ID_WEATHER_LAST           299U
#define GH_RTU_WEATHER_REG_COUNT           9U
#define GH_RTU_WEATHER_SYNC_BASE_REG       158U
#define GH_RTU_WEATHER_SYNC_AGE_REG        (GH_RTU_WEATHER_SYNC_BASE_REG + GH_RTU_WEATHER_REG_COUNT)
#define GH_RTU_WEATHER_SYNC_TOKEN_REG      (GH_RTU_WEATHER_SYNC_AGE_REG + 1U)
#define GH_RTU_WEATHER_APPLIED_TOKEN_REG   (GH_RTU_WEATHER_SYNC_TOKEN_REG + 1U)
#define GH_RTU_WEATHER_RESULT_REG          (GH_RTU_WEATHER_APPLIED_TOKEN_REG + 1U)
#define GH_RTU_WEATHER_SYNC_WRITE_REG_COUNT (GH_RTU_WEATHER_REG_COUNT + 2U)
#define GH_RTU_WEATHER_SYNC_RESULT_APPLIED 2U
#define GH_RTU_WEATHER_SYNC_RESULT_NOOP    5U
#define GH_RTU_WEATHER_SYNC_PERIOD_MS      CTRL_SYNC_PERIOD_MS
#define GH_RTU_WEATHER_SYNC_SOURCE_STALE_MS 30000UL
#define GH_RTU_WEATHER_SYNC_RETRY_BACKOFF_MS 1000UL
#define GH_RTU_WEATHER_SYNC_MAX_RETRIES    2U
#define GH_RTU_WEATHER_SYNC_ACK_POLL_ATTEMPTS 2U
#define GH_RTU_WEATHER_SYNC_ACK_POLL_DELAY_MS 50U
#define GH_CMD_IDLE_SERVICE_SLICE_MS        20U
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
#define GH_POLICY_REASON_NONE         0U
#define GH_POLICY_REASON_TIMEOUT      1U
#define GH_POLICY_REASON_CRC          2U
#define GH_POLICY_REASON_LINK_LOSS    3U
#define GH_POLICY_REASON_RECOVERED    4U

typedef enum
{
  GH_TOPOLOGY_CYCLE_OK = 0U,
  GH_TOPOLOGY_CYCLE_INVALID = 1U,
  GH_TOPOLOGY_CYCLE_EMPTY = 2U
} gh_topology_cycle_state_t;

static gh_topology_poll_req_t s_topology_plan_cache[GH_TOPOLOGY_V2_MAX_REQ_PROFILES] __attribute__((section(".ccmram")));
static gh_topology_point_binding_t s_topology_point_cache[GH_TOPOLOGY_V2_MAX_POINTS] __attribute__((section(".ccmram")));
static gh_topology_cmd_binding_t s_topology_cmd_cache[GH_TOPOLOGY_V2_MAX_COMMANDS] __attribute__((section(".ccmram")));
static gh_topology_policy_binding_t s_topology_policy_cache[GH_TOPOLOGY_V2_MAX_POLICIES] __attribute__((section(".ccmram")));
static bool s_slave_comm_up[MODBUS_MAX_SLAVES] = {false};
static uint32_t s_rtc_sync_last_ok_ms[MODBUS_MAX_SLAVES] = {0U};
static uint32_t s_rtc_sync_last_attempt_ms[MODBUS_MAX_SLAVES] = {0U};
static uint8_t s_rtc_sync_retry_count[MODBUS_MAX_SLAVES] = {0U};
static uint16_t s_rtc_sync_token_seq = 1U;
static uint32_t s_rtc_sync_attempt_count = 0U;
static uint32_t s_rtc_sync_success_count = 0U;
static uint32_t s_rtc_sync_fail_count = 0U;
static uint16_t s_rtc_sync_last_slave_id = 0U;
static uint16_t s_rtc_sync_last_token = 0U;
static uint16_t s_rtc_sync_last_result = 0U;
static uint32_t s_weather_sync_last_ok_ms[MODBUS_MAX_SLAVES] = {0U};
static uint32_t s_weather_sync_last_attempt_ms[MODBUS_MAX_SLAVES] = {0U};
static uint8_t s_weather_sync_retry_count[MODBUS_MAX_SLAVES] = {0U};
static uint16_t s_weather_sync_token_seq = 1U;
static uint16_t s_weather_sync_snapshot[GH_RTU_WEATHER_REG_COUNT] = {0U};
static uint32_t s_weather_sync_snapshot_updated_ms = 0U;
static uint8_t s_weather_sync_source_slave_id = 0U;
static bool s_weather_sync_snapshot_valid = false;
static uint16_t s_runtime_cmd_count = 0U;
static uint16_t s_runtime_point_count = 0U;
static bool s_runtime_topology_ready = false;

extern RTC_HandleTypeDef hrtc;

static uint8_t gh_slave_to_index(uint8_t slave_id);
static bool gh_slave_id_valid(uint8_t slave_id);
static bool gh_modbus_read_holding_retry(uint8_t slave_id,
                                         uint16_t start_reg,
                                         uint16_t reg_count,
                                         uint16_t *out_regs,
                                         uint32_t timeout_ms,
                                         uint8_t retries,
                                         uint8_t backoff_ms,
                                         modbus_io_error_t *out_last_err);
static bool gh_modbus_write_multi_retry(uint8_t slave_id,
                                        uint16_t start_reg,
                                        uint16_t reg_count,
                                        const uint16_t *regs,
                                        uint8_t retries,
                                        uint32_t timeout_ms);
static void gh_rtc_sync_publish_diag(void);
static void gh_rtc_sync_invalidate_all(void);
static void gh_weather_sync_invalidate_all(void);
static void gh_run_safe_mode(uint8_t *comm_fail_streak,
                             uint8_t *comm_ok_streak,
                             uint8_t fail_offline_cycles,
                             gh_topology_cycle_state_t cycle_state,
                             bool publish_diag_event);

static bool gh_get_server_rtc_hhmm(uint8_t *out_hour, uint8_t *out_minute)
{
  RTC_TimeTypeDef rtc_time = {0};
  RTC_DateTypeDef rtc_date = {0};

  if ((out_hour == NULL) || (out_minute == NULL))
  {
    return false;
  }

  if ((HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK) ||
      (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK))
  {
    return false;
  }

  *out_hour = rtc_time.Hours;
  *out_minute = rtc_time.Minutes;
  return true;
}

static uint16_t gh_rtc_sync_next_token(void)
{
  uint16_t token = s_rtc_sync_token_seq;

  s_rtc_sync_token_seq++;
  if (s_rtc_sync_token_seq == 0U)
  {
    s_rtc_sync_token_seq = 1U;
  }
  return token;
}

static bool gh_rtc_sync_wait_ack(uint8_t slave_id, uint16_t token)
{
  uint8_t attempt;
  uint16_t ack_regs[2] = {0U, 0U};

  for (attempt = 0U; attempt < GH_RTU_RTC_SYNC_ACK_POLL_ATTEMPTS; attempt++)
  {
    if (gh_modbus_read_holding_retry(slave_id,
                                     GH_RTU_RTC_APPLIED_TOKEN_REG,
                                     2U,
                                     ack_regs,
                                     MODBUS_RTU_RESP_TIMEOUT_MS,
                                     MODBUS_RETRY_COUNT,
                                     MODBUS_RETRY_BACKOFF_MS,
                                     NULL))
    {
      if (ack_regs[0] == token)
      {
        s_rtc_sync_last_result = ack_regs[1];
        return ((ack_regs[1] == GH_RTU_RTC_SYNC_RESULT_APPLIED) ||
                (ack_regs[1] == GH_RTU_RTC_SYNC_RESULT_NOOP));
      }
    }

    if ((attempt + 1U) < GH_RTU_RTC_SYNC_ACK_POLL_ATTEMPTS)
    {
      osDelay(GH_RTU_RTC_SYNC_ACK_POLL_DELAY_MS);
    }
  }

  return false;
}

static bool gh_rtc_sync_send(uint8_t slave_id, uint8_t hour, uint8_t minute, uint16_t token)
{
  uint16_t write_regs[3];

  write_regs[0] = (uint16_t)hour;
  write_regs[1] = (uint16_t)minute;
  write_regs[2] = token;

  if (!gh_modbus_write_multi_retry(slave_id,
                                   GH_RTU_RTC_SET_HOUR_REG,
                                   3U,
                                   write_regs,
                                   MODBUS_RETRY_COUNT,
                                   MODBUS_RTU_RESP_TIMEOUT_MS))
  {
    s_rtc_sync_last_result = 0xFFFEU; /* write failed */
    return false;
  }

  return gh_rtc_sync_wait_ack(slave_id, token);
}

static void gh_rtc_sync_publish_diag(void)
{
  GH_ModbusMap_ReportRtcSyncDiag(s_rtc_sync_attempt_count,
                                 s_rtc_sync_success_count,
                                 s_rtc_sync_fail_count,
                                 s_rtc_sync_last_slave_id,
                                 s_rtc_sync_last_token,
                                 s_rtc_sync_last_result);
}

static void gh_rtc_sync_invalidate_all(void)
{
  memset(s_rtc_sync_last_ok_ms, 0, sizeof(s_rtc_sync_last_ok_ms));
  memset(s_rtc_sync_last_attempt_ms, 0, sizeof(s_rtc_sync_last_attempt_ms));
  memset(s_rtc_sync_retry_count, 0, sizeof(s_rtc_sync_retry_count));
}

static bool gh_module_id_is_zone(uint16_t module_id)
{
  return (module_id >= GH_MODULE_ID_ZONE_FIRST) && (module_id <= GH_MODULE_ID_ZONE_LAST);
}

static bool gh_module_id_is_weather(uint16_t module_id)
{
  return (module_id >= GH_MODULE_ID_WEATHER_FIRST) && (module_id <= GH_MODULE_ID_WEATHER_LAST);
}

static void gh_weather_sync_invalidate_all(void)
{
  memset(s_weather_sync_last_ok_ms, 0, sizeof(s_weather_sync_last_ok_ms));
  memset(s_weather_sync_last_attempt_ms, 0, sizeof(s_weather_sync_last_attempt_ms));
  memset(s_weather_sync_retry_count, 0, sizeof(s_weather_sync_retry_count));
}

static void gh_update_weather_sync_snapshot(uint8_t slave_id, const uint16_t *regs, uint32_t now_ms)
{
  if (!gh_slave_id_valid(slave_id) || (regs == NULL))
  {
    return;
  }

  memcpy(s_weather_sync_snapshot, regs, sizeof(s_weather_sync_snapshot));
  s_weather_sync_snapshot_updated_ms = now_ms;
  s_weather_sync_source_slave_id = slave_id;
  s_weather_sync_snapshot_valid = true;
}

static uint16_t gh_weather_sync_next_token(void)
{
  uint16_t token = s_weather_sync_token_seq;

  s_weather_sync_token_seq++;
  if (s_weather_sync_token_seq == 0U)
  {
    s_weather_sync_token_seq = 1U;
  }
  return token;
}

static bool gh_weather_sync_wait_ack(uint8_t slave_id, uint16_t token)
{
  uint8_t attempt;
  uint16_t ack_regs[2] = {0U, 0U};

  for (attempt = 0U; attempt < GH_RTU_WEATHER_SYNC_ACK_POLL_ATTEMPTS; attempt++)
  {
    if (gh_modbus_read_holding_retry(slave_id,
                                     GH_RTU_WEATHER_APPLIED_TOKEN_REG,
                                     2U,
                                     ack_regs,
                                     MODBUS_RTU_RESP_TIMEOUT_MS,
                                     1U,
                                     MODBUS_RETRY_BACKOFF_MS,
                                     NULL))
    {
      if (ack_regs[0] == token)
      {
        return ((ack_regs[1] == GH_RTU_WEATHER_SYNC_RESULT_APPLIED) ||
                (ack_regs[1] == GH_RTU_WEATHER_SYNC_RESULT_NOOP));
      }
    }

    if ((attempt + 1U) < GH_RTU_WEATHER_SYNC_ACK_POLL_ATTEMPTS)
    {
      osDelay(GH_RTU_WEATHER_SYNC_ACK_POLL_DELAY_MS);
    }
  }

  return false;
}

static bool gh_weather_sync_send(uint8_t slave_id, uint32_t now_ms, uint16_t token)
{
  uint16_t write_regs[GH_RTU_WEATHER_SYNC_WRITE_REG_COUNT];
  uint32_t age_ms;
  uint32_t age_s;

  if (!s_weather_sync_snapshot_valid || (s_weather_sync_snapshot_updated_ms == 0U))
  {
    return false;
  }

  memcpy(write_regs, s_weather_sync_snapshot, sizeof(s_weather_sync_snapshot));
  age_ms = now_ms - s_weather_sync_snapshot_updated_ms;
  age_s = age_ms / 1000U;
  if (age_s > 0xFFFFUL)
  {
    age_s = 0xFFFFUL;
  }
  write_regs[GH_RTU_WEATHER_REG_COUNT] = (uint16_t)age_s;
  write_regs[GH_RTU_WEATHER_REG_COUNT + 1U] = token;

  if (!gh_modbus_write_multi_retry(slave_id,
                                   GH_RTU_WEATHER_SYNC_BASE_REG,
                                   GH_RTU_WEATHER_SYNC_WRITE_REG_COUNT,
                                   write_regs,
                                   1U,
                                   MODBUS_RTU_RESP_TIMEOUT_MS))
  {
    return false;
  }

  return gh_weather_sync_wait_ack(slave_id, token);
}

static void gh_maybe_sync_slave_weather(uint8_t slave_id, uint32_t now_ms, bool force_sync)
{
  uint8_t slave_idx;
  uint16_t token;
  bool sync_ok;

  if (!gh_slave_id_valid(slave_id) ||
      !s_weather_sync_snapshot_valid ||
      (s_weather_sync_source_slave_id == 0U) ||
      (slave_id == s_weather_sync_source_slave_id))
  {
    return;
  }

  if ((uint32_t)(now_ms - s_weather_sync_snapshot_updated_ms) > GH_RTU_WEATHER_SYNC_SOURCE_STALE_MS)
  {
    return;
  }

  slave_idx = gh_slave_to_index(slave_id);
  if (!force_sync)
  {
    if (s_weather_sync_retry_count[slave_idx] > 0U)
    {
      if (s_weather_sync_retry_count[slave_idx] >= GH_RTU_WEATHER_SYNC_MAX_RETRIES)
      {
        if ((uint32_t)(now_ms - s_weather_sync_last_attempt_ms[slave_idx]) < GH_RTU_WEATHER_SYNC_PERIOD_MS)
        {
          return;
        }
        s_weather_sync_retry_count[slave_idx] = 0U;
      }
      else if ((uint32_t)(now_ms - s_weather_sync_last_attempt_ms[slave_idx]) < GH_RTU_WEATHER_SYNC_RETRY_BACKOFF_MS)
      {
        return;
      }
    }
    else if ((s_weather_sync_last_ok_ms[slave_idx] != 0U) &&
             ((uint32_t)(now_ms - s_weather_sync_last_ok_ms[slave_idx]) < GH_RTU_WEATHER_SYNC_PERIOD_MS))
    {
      return;
    }
  }

  token = gh_weather_sync_next_token();
  sync_ok = gh_weather_sync_send(slave_id, now_ms, token);
  s_weather_sync_last_attempt_ms[slave_idx] = now_ms;
  if (sync_ok)
  {
    s_weather_sync_last_ok_ms[slave_idx] = now_ms;
    s_weather_sync_retry_count[slave_idx] = 0U;
    return;
  }

  if (s_weather_sync_retry_count[slave_idx] < 0xFFU)
  {
    s_weather_sync_retry_count[slave_idx]++;
  }
}

static void gh_maybe_sync_slave_rtc(uint8_t slave_id, uint32_t now_ms, bool force_sync)
{
  uint8_t slave_idx;
  uint8_t hour;
  uint8_t minute;
  uint16_t token;
  bool sync_ok;

  if (!gh_slave_id_valid(slave_id))
  {
    return;
  }

  slave_idx = gh_slave_to_index(slave_id);
  if (!force_sync)
  {
    if (s_rtc_sync_retry_count[slave_idx] > 0U)
    {
      if (s_rtc_sync_retry_count[slave_idx] >= GH_RTU_RTC_SYNC_MAX_RETRIES)
      {
        if ((uint32_t)(now_ms - s_rtc_sync_last_attempt_ms[slave_idx]) < GH_RTU_RTC_SYNC_PERIOD_MS)
        {
          return;
        }
        s_rtc_sync_retry_count[slave_idx] = 0U;
      }
      else if ((uint32_t)(now_ms - s_rtc_sync_last_attempt_ms[slave_idx]) < GH_RTU_RTC_SYNC_RETRY_BACKOFF_MS)
      {
        return;
      }
    }
    else if ((s_rtc_sync_last_ok_ms[slave_idx] != 0U) &&
             ((uint32_t)(now_ms - s_rtc_sync_last_ok_ms[slave_idx]) < GH_RTU_RTC_SYNC_PERIOD_MS))
    {
      return;
    }
  }

  if (!gh_get_server_rtc_hhmm(&hour, &minute))
  {
    return;
  }

  token = gh_rtc_sync_next_token();
  s_rtc_sync_attempt_count++;
  s_rtc_sync_last_slave_id = slave_id;
  s_rtc_sync_last_token = token;
  s_rtc_sync_last_result = 0xFFFFU; /* pending */
  sync_ok = gh_rtc_sync_send(slave_id, hour, minute, token);
  s_rtc_sync_last_attempt_ms[slave_idx] = now_ms;
  if (sync_ok)
  {
    s_rtc_sync_last_ok_ms[slave_idx] = now_ms;
    s_rtc_sync_retry_count[slave_idx] = 0U;
    s_rtc_sync_success_count++;
    gh_rtc_sync_publish_diag();
    return;
  }

  if (s_rtc_sync_retry_count[slave_idx] < 0xFFU)
  {
    s_rtc_sync_retry_count[slave_idx]++;
  }
  s_rtc_sync_fail_count++;
  if (s_rtc_sync_last_result == 0xFFFFU)
  {
    s_rtc_sync_last_result = 0xFFFD; /* ack timeout/no token match */
  }
  gh_rtc_sync_publish_diag();
}

static void gh_refresh_rtc_clock_registers(void)
{
  static uint16_t s_last_hhmm = 0xFFFFU;
  uint8_t hour;
  uint8_t minute;
  uint16_t hhmm;

  if (!gh_get_server_rtc_hhmm(&hour, &minute))
  {
    return;
  }

  hhmm = (uint16_t)(((uint16_t)hour * 60U) + (uint16_t)minute);
  if (hhmm == s_last_hhmm)
  {
    return;
  }

  s_last_hhmm = hhmm;
  GH_ModbusMap_UpdateRtcTime(hour, minute);
}

static void gh_apply_rtc_set_request(void)
{
  gh_rtc_set_request_t req = {0};
  RTC_TimeTypeDef rtc_time = {0};
  bool applied = false;

  if (!GH_ModbusMap_GetRtcSetRequest(&req))
  {
    return;
  }

  rtc_time.Hours = req.hour;
  rtc_time.Minutes = req.minute;
  rtc_time.Seconds = 0U;
  rtc_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  rtc_time.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) == HAL_OK)
  {
    applied = true;
    gh_rtc_sync_invalidate_all();
  }

  GH_ModbusMap_MarkRtcSetResult(req.token, applied, req.hour, req.minute);
}

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

static bool gh_modbus_read_holding_retry(uint8_t slave_id,
                                         uint16_t start_reg,
                                         uint16_t reg_count,
                                         uint16_t *out_regs,
                                         uint32_t timeout_ms,
                                         uint8_t retries,
                                         uint8_t backoff_ms,
                                         modbus_io_error_t *out_last_err)
{
  uint8_t attempt;
  uint8_t retry_count;
  modbus_io_error_t last_err = MODBUS_IO_ERR_NONE;

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
      if (out_last_err != NULL)
      {
        *out_last_err = MODBUS_IO_ERR_NONE;
      }
      return true;
    }
    last_err = modbus_get_last_error();
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if ((attempt + 1U) < retry_count)
    {
      osDelay((uint32_t)backoff_ms * (attempt + 1U));
    }
  }

  if (out_last_err != NULL)
  {
    *out_last_err = last_err;
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

static void gh_set_quality_for_slave(uint8_t slave_id,
                                     uint8_t quality,
                                     const gh_topology_point_binding_t *points,
                                     uint16_t point_count)
{
  if ((points != NULL) && (point_count > 0U))
  {
    gh_set_points_quality_for_slave(slave_id, quality, points, point_count);
  }
  else
  {
    gh_set_slave_quality(slave_id, quality);
  }
}

static const gh_topology_policy_binding_t *gh_find_policy_for_slave(const gh_topology_policy_binding_t *policies,
                                                                    uint16_t policy_count,
                                                                    uint8_t slave_id)
{
  uint16_t i;

  if ((policies == NULL) || (policy_count == 0U))
  {
    return NULL;
  }

  for (i = 0U; i < policy_count; i++)
  {
    if (policies[i].slave_id == slave_id)
    {
      return &policies[i];
    }
  }

  return NULL;
}

static uint8_t gh_policy_map_error_reason(modbus_io_error_t err)
{
  if (err == MODBUS_IO_ERR_CRC)
  {
    return GH_POLICY_REASON_CRC;
  }
  return GH_POLICY_REASON_TIMEOUT;
}

static uint16_t gh_policy_recover_cycles(const gh_topology_policy_binding_t *policy)
{
  if ((policy == NULL) || (policy->recover_good_cycles == 0U))
  {
    return GH_QUALITY_RECOVER_OK_CYCLES;
  }
  return policy->recover_good_cycles;
}

static uint16_t gh_policy_link_loss_threshold(const gh_topology_policy_binding_t *policy, uint8_t default_fail_cycles)
{
  if ((policy == NULL) || (policy->max_consecutive_fail == 0U))
  {
    return default_fail_cycles;
  }
  return policy->max_consecutive_fail;
}

static uint8_t gh_policy_action_for_reason(const gh_topology_policy_binding_t *policy, uint8_t reason)
{
  if (policy == NULL)
  {
    return GH_TOPOLOGY_POLICY_ACTION_KEEP_LAST;
  }

  if (reason == GH_POLICY_REASON_LINK_LOSS)
  {
    return policy->on_link_loss;
  }
  if (reason == GH_POLICY_REASON_CRC)
  {
    return policy->on_crc_error;
  }
  return policy->on_timeout;
}

static uint8_t gh_policy_apply_action(uint8_t slave_id,
                                      uint8_t action,
                                      const gh_topology_point_binding_t *points,
                                      uint16_t point_count)
{
  switch (action)
  {
    case GH_TOPOLOGY_POLICY_ACTION_KEEP_LAST:
      return SENSOR_QUALITY_STALE;
    case GH_TOPOLOGY_POLICY_ACTION_SAFE_DEFAULT:
      gh_set_quality_for_slave(slave_id, SENSOR_QUALITY_FAULT, points, point_count);
      return SENSOR_QUALITY_FAULT;
    case GH_TOPOLOGY_POLICY_ACTION_FORCE_OFFLINE:
      gh_set_quality_for_slave(slave_id, SENSOR_QUALITY_OFFLINE, points, point_count);
      return SENSOR_QUALITY_OFFLINE;
    default:
      gh_set_quality_for_slave(slave_id, SENSOR_QUALITY_STALE, points, point_count);
      return SENSOR_QUALITY_STALE;
  }
}

static void gh_policy_log_transition(uint8_t slave_id,
                                     uint8_t reason,
                                     uint8_t action,
                                     uint16_t fail_streak,
                                     uint8_t *last_reason,
                                     uint8_t *last_action)
{
  uint8_t idx;
  float event_value;
  uint8_t severity;

  if ((last_reason == NULL) || (last_action == NULL) || !gh_slave_id_valid(slave_id))
  {
    return;
  }

  idx = gh_slave_to_index(slave_id);
  if ((last_reason[idx] == reason) && (last_action[idx] == action))
  {
    return;
  }

  last_reason[idx] = reason;
  last_action[idx] = action;
  event_value = (float)(((uint32_t)reason * 1000U) + ((uint32_t)action * 100U) + (uint32_t)fail_streak);
  severity = (reason == GH_POLICY_REASON_LINK_LOSS) ? EVENT_SEV_ALARM : EVENT_SEV_WARN;
  publish_event(severity, EVENT_CODE_CTRL_SYNC_FAIL, slave_id, event_value);
  g_status.last_error_code = EVENT_CODE_CTRL_SYNC_FAIL;
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
                                      MODBUS_RETRY_BACKOFF_MS,
                                      NULL);
}

static const gh_topology_cmd_binding_t *gh_find_command_profile(const gh_topology_cmd_binding_t *cmds,
                                                                uint16_t cmd_count,
                                                                const gh_data_driven_command_request_t *req)
{
  uint16_t i;

  if ((cmds == NULL) || (req == NULL) || (cmd_count == 0U))
  {
    return NULL;
  }

  for (i = 0U; i < cmd_count; i++)
  {
    if ((cmds[i].cmd_id == req->cmd_profile_id) &&
        (cmds[i].slave_id == req->slave_id) &&
        (cmds[i].module_id == req->module_id))
    {
      return &cmds[i];
    }
  }

  return NULL;
}

static bool gh_hhmm_is_valid(uint16_t hhmm)
{
  uint16_t hh = (uint16_t)(hhmm / 100U);
  uint16_t mm = (uint16_t)(hhmm % 100U);

  return (hh < 24U) && (mm < 60U);
}

static uint8_t gh_command_kind(const gh_topology_cmd_binding_t *cmd)
{
  if (cmd == NULL)
  {
    return GH_TOPOLOGY_CMD_KIND_GENERIC;
  }

  return GH_TOPOLOGY_CMD_KIND_FROM_FLAGS(cmd->flags);
}

static const gh_topology_cmd_binding_t *gh_find_next_schedule_step(const gh_topology_cmd_binding_t *cmds,
                                                                   uint16_t cmd_count,
                                                                   const gh_topology_cmd_binding_t *current)
{
  const gh_topology_cmd_binding_t *best = NULL;
  uint16_t i;
  uint16_t next_offset;

  if ((cmds == NULL) || (current == NULL) || (cmd_count == 0U))
  {
    return NULL;
  }
  if (((uint32_t)current->payload_offset + (uint32_t)current->reg_count) > 0xFFFFU)
  {
    return NULL;
  }

  next_offset = (uint16_t)((uint32_t)current->payload_offset + (uint32_t)current->reg_count);
  for (i = 0U; i < cmd_count; i++)
  {
    if ((cmds[i].slave_id != current->slave_id) ||
        (cmds[i].module_id != current->module_id) ||
        (gh_command_kind(&cmds[i]) != GH_TOPOLOGY_CMD_KIND_SCHEDULE) ||
        (cmds[i].payload_offset != next_offset))
    {
      continue;
    }
    if ((best == NULL) ||
        (cmds[i].cmd_id < best->cmd_id) ||
        ((cmds[i].cmd_id == best->cmd_id) && (cmds[i].start_reg < best->start_reg)))
    {
      best = &cmds[i];
    }
  }

  return best;
}

static bool gh_validate_schedule_step(const gh_data_driven_command_request_t *req,
                                      const gh_topology_cmd_binding_t *cmd)
{
  uint16_t slot;
  uint16_t base;

  if ((req == NULL) || (cmd == NULL))
  {
    return false;
  }
  if ((req->payload_len < GH_SCHEDULE_PAYLOAD_REQUIRED_WORDS) ||
      (req->payload_len > TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS) ||
      (req->payload[GH_SCHEDULE_PAYLOAD_OFF_CMD_KIND] != GH_MB_SCHED_CMD_KIND_REMOTE_SCHEDULE))
  {
    return false;
  }
  if (gh_command_kind(cmd) != GH_TOPOLOGY_CMD_KIND_SCHEDULE)
  {
    return false;
  }
  if (((uint32_t)cmd->payload_offset + (uint32_t)cmd->reg_count) > req->payload_len)
  {
    return false;
  }
  if ((cmd->fc == GH_CMD_FC_WRITE_MULTI) && (cmd->reg_count == GH_SCHEDULE_PAYLOAD_REG_COUNT))
  {
    for (slot = 0U; slot < GH_SCHEDULE_SLOT_COUNT; slot++)
    {
      base = (uint16_t)((uint16_t)cmd->payload_offset + (uint16_t)(slot * GH_SCHEDULE_SLOT_WORDS));
      if ((req->payload[base + 0U] > 1U) ||
          !gh_hhmm_is_valid(req->payload[base + 1U]) ||
          !gh_hhmm_is_valid(req->payload[base + 2U]))
      {
        return false;
      }
    }
    return true;
  }
  if ((cmd->fc == GH_CMD_FC_WRITE_SINGLE) && (cmd->reg_count == 1U))
  {
    return true;
  }

  return false;
}

static bool gh_execute_data_driven_step(const gh_data_driven_command_request_t *req,
                                        const gh_topology_cmd_binding_t *cmd,
                                        const gh_topology_point_binding_t *points,
                                        uint16_t point_count,
                                        bool exact_len,
                                        gh_data_driven_command_result_t *out_result)
{
  uint32_t timeout_ms;
  bool ok = false;
  uint16_t cmd_data_len;

  if ((req == NULL) || (cmd == NULL) || (out_result == NULL))
  {
    return false;
  }
  if ((req->payload_len == 0U) || (req->payload_len > TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS))
  {
    out_result->result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
    return false;
  }
  if ((cmd->reg_count == 0U) ||
      (cmd->payload_offset >= TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS) ||
      (((uint32_t)cmd->payload_offset + (uint32_t)cmd->reg_count) > TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS))
  {
    out_result->result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
    return false;
  }
  if (cmd->payload_offset >= req->payload_len)
  {
    out_result->result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
    return false;
  }

  if (exact_len)
  {
    if (((uint32_t)cmd->payload_offset + (uint32_t)cmd->reg_count) > req->payload_len)
    {
      out_result->result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
      return false;
    }
    cmd_data_len = cmd->reg_count;
  }
  else
  {
    cmd_data_len = (uint16_t)(req->payload_len - cmd->payload_offset);
    if ((cmd_data_len == 0U) || (cmd_data_len > cmd->reg_count))
    {
      out_result->result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
      return false;
    }
  }

  if ((cmd->fc != GH_CMD_FC_WRITE_SINGLE) && (cmd->fc != GH_CMD_FC_WRITE_MULTI))
  {
    out_result->result = GH_MB_DCMD_RESULT_REJECT_FC;
    return false;
  }

  timeout_ms = (cmd->timeout_ms == 0U) ? MODBUS_RTU_RESP_TIMEOUT_MS : cmd->timeout_ms;
  if (cmd->fc == GH_CMD_FC_WRITE_SINGLE)
  {
    if ((cmd_data_len != 1U) || (cmd->reg_count != 1U))
    {
      out_result->result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
      return false;
    }
    ok = gh_modbus_write_single_retry(req->slave_id,
                                      cmd->start_reg,
                                      req->payload[cmd->payload_offset],
                                      cmd->retries,
                                      timeout_ms);
  }
  else
  {
    ok = gh_modbus_write_multi_retry(req->slave_id,
                                     cmd->start_reg,
                                     cmd_data_len,
                                     &req->payload[cmd->payload_offset],
                                     cmd->retries,
                                     timeout_ms);
  }

  if (!ok)
  {
    out_result->result = GH_MB_DCMD_RESULT_TRANSPORT_FAIL;
    out_result->io_error = modbus_get_last_error();
    return false;
  }

  if ((cmd->ack_point_id != 0U) &&
      !gh_command_ack_probe(req->slave_id, cmd, points, point_count))
  {
    out_result->result = GH_MB_DCMD_RESULT_ACK_FAIL;
    out_result->io_error = MODBUS_IO_ERR_NONE;
    return false;
  }

  out_result->result = GH_MB_DCMD_RESULT_APPLIED;
  out_result->io_error = MODBUS_IO_ERR_NONE;
  return true;
}

static bool gh_schedule_verify_apply_status(const gh_data_driven_command_request_t *req,
                                            const gh_topology_cmd_binding_t *cmd,
                                            gh_data_driven_command_result_t *out_result)
{
  uint16_t apply_regs[GH_RTU_APPLY_STATUS_REG_COUNT] = {0U};
  uint32_t expected_ver;
  uint32_t active_ver;
  modbus_io_error_t read_err = MODBUS_IO_ERR_NONE;

  if ((req == NULL) || (cmd == NULL) || (out_result == NULL))
  {
    return false;
  }
  if ((cmd->fc != GH_CMD_FC_WRITE_SINGLE) || (cmd->start_reg != GH_RTU_APPLY_TRIGGER_REG))
  {
    return true;
  }

  if (!gh_modbus_read_holding_retry(req->slave_id,
                                    GH_RTU_APPLY_STATUS_REG,
                                    GH_RTU_APPLY_STATUS_REG_COUNT,
                                    apply_regs,
                                    cmd->timeout_ms,
                                    cmd->retries,
                                    MODBUS_RETRY_BACKOFF_MS,
                                    &read_err))
  {
    out_result->result = GH_MB_DCMD_RESULT_TRANSPORT_FAIL;
    out_result->io_error = read_err;
    return false;
  }

  g_status.last_apply_status = (uint8_t)(apply_regs[0] & 0x00FFU);
  if (apply_regs[0] != APPLY_APPLIED)
  {
    out_result->result = GH_MB_DCMD_RESULT_ACK_FAIL;
    out_result->io_error = MODBUS_IO_ERR_NONE;
    return false;
  }

  expected_ver = ((uint32_t)req->payload[GH_SCHEDULE_PAYLOAD_OFF_EXPECTED_VER_HI] << 16U) |
                 (uint32_t)req->payload[GH_SCHEDULE_PAYLOAD_OFF_EXPECTED_VER_LO];
  active_ver = ((uint32_t)apply_regs[1] << 16U) | (uint32_t)apply_regs[2];
#if (GH_SCHEDULE_VERSION_CHECK_ALLOW_DISABLED == 1U)
  if ((expected_ver != 0U) && (active_ver != expected_ver))
#else
  if (active_ver != expected_ver)
#endif
  {
    out_result->result = GH_MB_DCMD_RESULT_ACK_FAIL;
    out_result->io_error = MODBUS_IO_ERR_NONE;
    return false;
  }

  return true;
}

static bool gh_execute_data_driven_command(const gh_data_driven_command_request_t *req,
                                           const gh_topology_cmd_binding_t *cmds,
                                           uint16_t cmd_count,
                                           const gh_topology_point_binding_t *points,
                                           uint16_t point_count,
                                           gh_data_driven_command_result_t *out_result)
{
  const gh_topology_cmd_binding_t *cmd;
  const gh_topology_cmd_binding_t *apply_step;
  uint8_t cmd_kind;

  if ((req == NULL) || (out_result == NULL))
  {
    return false;
  }

  memset(out_result, 0, sizeof(*out_result));
  out_result->trigger = req->trigger;
  out_result->result = GH_MB_DCMD_RESULT_REJECT_TOPOLOGY;
  out_result->io_error = MODBUS_IO_ERR_NONE;

  cmd = gh_find_command_profile(cmds, cmd_count, req);
  if (cmd == NULL)
  {
    return false;
  }
  cmd_kind = gh_command_kind(cmd);
  if (cmd_kind > GH_TOPOLOGY_CMD_KIND_MAX)
  {
    return false;
  }
  if (cmd_kind != GH_TOPOLOGY_CMD_KIND_SCHEDULE)
  {
    return gh_execute_data_driven_step(req, cmd, points, point_count, false, out_result);
  }

  if ((cmd->fc != GH_CMD_FC_WRITE_MULTI) ||
      (cmd->payload_offset != 0U) ||
      (cmd->reg_count != GH_SCHEDULE_PAYLOAD_REG_COUNT))
  {
    out_result->result = GH_MB_DCMD_RESULT_REJECT_TOPOLOGY;
    out_result->io_error = MODBUS_IO_ERR_NONE;
    return false;
  }
  apply_step = gh_find_next_schedule_step(cmds, cmd_count, cmd);
  if ((apply_step == NULL) ||
      (apply_step->fc != GH_CMD_FC_WRITE_SINGLE) ||
      (apply_step->payload_offset != GH_SCHEDULE_PAYLOAD_OFF_APPLY_VALUE) ||
      (apply_step->reg_count != 1U) ||
      (apply_step->start_reg != GH_RTU_APPLY_TRIGGER_REG))
  {
    out_result->result = GH_MB_DCMD_RESULT_REJECT_TOPOLOGY;
    out_result->io_error = MODBUS_IO_ERR_NONE;
    return false;
  }
  if (!gh_validate_schedule_step(req, cmd) || !gh_validate_schedule_step(req, apply_step))
  {
    out_result->result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
    out_result->io_error = MODBUS_IO_ERR_NONE;
    return false;
  }
  if (!gh_execute_data_driven_step(req, cmd, points, point_count, true, out_result))
  {
    return false;
  }
  if (!gh_execute_data_driven_step(req, apply_step, points, point_count, true, out_result))
  {
    return false;
  }
  if (!gh_schedule_verify_apply_status(req, apply_step, out_result))
  {
    return false;
  }

  out_result->result = GH_MB_DCMD_RESULT_APPLIED;
  out_result->io_error = MODBUS_IO_ERR_NONE;
  return true;
}

static void gh_process_data_driven_command(const gh_topology_cmd_binding_t *cmds,
                                           uint16_t cmd_count,
                                           const gh_topology_point_binding_t *points,
                                           uint16_t point_count,
                                           bool topology_ready)
{
  gh_data_driven_command_request_t req = {0};
  gh_data_driven_command_result_t result = {0};
  bool ok = false;
  uint16_t event_code = EVENT_CODE_CTRL_SYNC_FAIL;

  if (!GH_ModbusMap_GetDataDrivenCommandRequest(&req))
  {
    return;
  }

  result.trigger = req.trigger;
  result.result = GH_MB_DCMD_RESULT_REJECT_TOPOLOGY;
  result.io_error = MODBUS_IO_ERR_NONE;
  if (topology_ready && (cmds != NULL) && (cmd_count > 0U))
  {
    ok = gh_execute_data_driven_command(&req, cmds, cmd_count, points, point_count, &result);
  }
  else
  {
    ok = false;
    result.result = GH_MB_DCMD_RESULT_REJECT_TOPOLOGY;
  }

  GH_ModbusMap_MarkDataDrivenCommandResult(&result);
  if (ok)
  {
    return;
  }

  if ((result.result == GH_MB_DCMD_RESULT_REJECT_TOPOLOGY) ||
      (result.result == GH_MB_DCMD_RESULT_REJECT_FC) ||
      (result.result == GH_MB_DCMD_RESULT_REJECT_BOUNDS) ||
      (result.result == GH_MB_DCMD_RESULT_REJECT_PARTIAL))
  {
    event_code = EVENT_CODE_CTRL_SYNC_TOPOLOGY_CONTRACT;
  }
  publish_event(EVENT_SEV_WARN, event_code, req.slave_id, (float)result.result);
  g_status.last_error_code = event_code;
}

static void gh_service_pending_commands_during_idle(uint32_t idle_ms)
{
  uint32_t slice_ms;

  while (idle_ms > 0U)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    gh_process_data_driven_command(s_runtime_topology_ready ? s_topology_cmd_cache : NULL,
                                   s_runtime_cmd_count,
                                   s_runtime_topology_ready ? s_topology_point_cache : NULL,
                                   s_runtime_point_count,
                                   s_runtime_topology_ready);

    slice_ms = (idle_ms > GH_CMD_IDLE_SERVICE_SLICE_MS) ? GH_CMD_IDLE_SERVICE_SLICE_MS : idle_ms;
    osDelay(slice_ms);
    idle_ms -= slice_ms;
  }
}

static gh_topology_cycle_state_t gh_run_topology_cycle(uint8_t *comm_fail_streak,
                                                       uint8_t *comm_ok_streak,
                                                       uint8_t fail_offline_cycles)
{
  bool slave_attempted[MODBUS_MAX_SLAVES] = {false};
  bool slave_success[MODBUS_MAX_SLAVES] = {false};
  uint8_t slave_fail_reason[MODBUS_MAX_SLAVES] = {GH_POLICY_REASON_NONE};
  uint16_t regs[MODBUS_MAX_REGS_PER_REQ];
  uint16_t sens[GH_RTU_SENSORS_PER_SLAVE];
  static uint16_t s_last_sens[MODBUS_MAX_SLAVES][GH_RTU_SENSORS_PER_SLAVE] = {{0U}};
  static uint32_t s_next_due_ms[GH_TOPOLOGY_V2_MAX_REQ_PROFILES] = {0U};
  static uint32_t s_last_plan_generation = 0U;
  static uint16_t s_last_plan_count = 0U;
  static uint8_t s_last_policy_reason[MODBUS_MAX_SLAVES] = {GH_POLICY_REASON_NONE};
  static uint8_t s_last_policy_action[MODBUS_MAX_SLAVES] = {GH_TOPOLOGY_POLICY_ACTION_KEEP_LAST};
  uint16_t plan_count = 0U;
  uint16_t point_count = 0U;
  uint16_t cmd_count = 0U;
  uint16_t policy_count = 0U;
  uint32_t plan_generation = 0U;
  uint32_t point_generation = 0U;
  uint32_t cmd_generation = 0U;
  uint32_t policy_generation = 0U;
  uint32_t plan_slave_mask = 0U;
  uint32_t zone_slave_mask = 0U;
  uint16_t i;
  uint16_t recover_cycles;
  uint16_t link_loss_threshold;
  uint16_t w;
  uint16_t valid_mask = 0U;
  uint32_t now_ms = HAL_GetTick();
  uint8_t slave_id;
  uint8_t slave_idx;
  uint8_t weather_source_slave_id = 0U;
  bool ok;
  uint8_t quality;
  uint8_t reason;
  uint8_t action;
  const gh_topology_policy_binding_t *policy;
  modbus_io_error_t read_err = MODBUS_IO_ERR_NONE;

  if (!GH_TopologyRuntime_CopyPollPlan(s_topology_plan_cache,
                                       GH_TOPOLOGY_V2_MAX_REQ_PROFILES,
                                       &plan_count,
                                       &plan_generation,
                                       &plan_slave_mask))
  {
    return GH_TOPOLOGY_CYCLE_INVALID;
  }
  if (!GH_TopologyRuntime_CopyPointBindings(s_topology_point_cache,
                                            GH_TOPOLOGY_V2_MAX_POINTS,
                                            &point_count,
                                            &point_generation))
  {
    return GH_TOPOLOGY_CYCLE_INVALID;
  }
  if (!GH_TopologyRuntime_CopyCommandBindings(s_topology_cmd_cache,
                                              GH_TOPOLOGY_V2_MAX_COMMANDS,
                                              &cmd_count,
                                              &cmd_generation))
  {
    return GH_TOPOLOGY_CYCLE_INVALID;
  }
  if (!GH_TopologyRuntime_CopyPolicyBindings(s_topology_policy_cache,
                                             GH_TOPOLOGY_V2_MAX_POLICIES,
                                             &policy_count,
                                             &policy_generation))
  {
    return GH_TOPOLOGY_CYCLE_INVALID;
  }
  if ((point_generation != plan_generation) ||
      (cmd_generation != plan_generation) ||
      (policy_generation != plan_generation))
  {
    if (!GH_TopologyRuntime_CopyPollPlan(s_topology_plan_cache,
                                         GH_TOPOLOGY_V2_MAX_REQ_PROFILES,
                                         &plan_count,
                                         &plan_generation,
                                         &plan_slave_mask))
    {
      return GH_TOPOLOGY_CYCLE_INVALID;
    }
    if (!GH_TopologyRuntime_CopyPointBindings(s_topology_point_cache,
                                              GH_TOPOLOGY_V2_MAX_POINTS,
                                              &point_count,
                                              &point_generation))
    {
      return GH_TOPOLOGY_CYCLE_INVALID;
    }
    if (!GH_TopologyRuntime_CopyCommandBindings(s_topology_cmd_cache,
                                                GH_TOPOLOGY_V2_MAX_COMMANDS,
                                                &cmd_count,
                                                &cmd_generation))
    {
      return GH_TOPOLOGY_CYCLE_INVALID;
    }
    if (!GH_TopologyRuntime_CopyPolicyBindings(s_topology_policy_cache,
                                               GH_TOPOLOGY_V2_MAX_POLICIES,
                                               &policy_count,
                                               &policy_generation))
    {
      return GH_TOPOLOGY_CYCLE_INVALID;
    }
    if ((plan_count == 0U) ||
        (point_generation != plan_generation) ||
        (cmd_generation != plan_generation) ||
        (policy_generation != plan_generation))
    {
      if (plan_count == 0U)
      {
        return GH_TOPOLOGY_CYCLE_EMPTY;
      }
      return GH_TOPOLOGY_CYCLE_INVALID;
    }
  }

  if (plan_count == 0U)
  {
    return GH_TOPOLOGY_CYCLE_EMPTY;
  }

  if ((plan_generation != s_last_plan_generation) || (plan_count != s_last_plan_count))
  {
    memset(s_next_due_ms, 0, sizeof(s_next_due_ms));
    memset(s_last_policy_reason, GH_POLICY_REASON_NONE, sizeof(s_last_policy_reason));
    memset(s_last_policy_action, GH_TOPOLOGY_POLICY_ACTION_KEEP_LAST, sizeof(s_last_policy_action));
    gh_weather_sync_invalidate_all();
    s_last_plan_generation = plan_generation;
    s_last_plan_count = plan_count;
  }

  for (i = 0U; i < plan_count; i++)
  {
    slave_id = s_topology_plan_cache[i].slave_id;
    if (!gh_slave_id_valid(slave_id))
    {
      continue;
    }
    if (gh_module_id_is_zone(s_topology_plan_cache[i].module_id))
    {
      zone_slave_mask |= (1UL << (uint32_t)(slave_id - 1U));
    }
    else if ((weather_source_slave_id == 0U) &&
             gh_module_id_is_weather(s_topology_plan_cache[i].module_id))
    {
      weather_source_slave_id = slave_id;
    }
  }
  if ((weather_source_slave_id == 0U) ||
      ((s_weather_sync_source_slave_id != 0U) &&
       (weather_source_slave_id != s_weather_sync_source_slave_id)))
  {
    s_weather_sync_snapshot_valid = false;
    s_weather_sync_snapshot_updated_ms = 0U;
    s_weather_sync_source_slave_id = 0U;
  }

  s_runtime_cmd_count = cmd_count;
  s_runtime_point_count = point_count;
  s_runtime_topology_ready = true;
  gh_process_data_driven_command(s_topology_cmd_cache,
                                 cmd_count,
                                 s_topology_point_cache,
                                 point_count,
                                 true);

  for (i = 0U; i < plan_count; i++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    gh_process_data_driven_command(s_topology_cmd_cache,
                                   cmd_count,
                                   s_topology_point_cache,
                                   point_count,
                                   true);
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
                                      s_topology_plan_cache[i].backoff_ms,
                                      &read_err);
    s_next_due_ms[i] = now_ms + s_topology_plan_cache[i].period_ms;

    if (!ok)
    {
      if (slave_fail_reason[slave_idx] == GH_POLICY_REASON_NONE)
      {
        slave_fail_reason[slave_idx] = gh_policy_map_error_reason(read_err);
      }
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
    if ((slave_id == weather_source_slave_id) &&
        gh_module_id_is_weather(s_topology_plan_cache[i].module_id) &&
        (s_topology_plan_cache[i].telemetry_word_count >= GH_RTU_WEATHER_REG_COUNT))
    {
      gh_update_weather_sync_snapshot(slave_id, regs, now_ms);
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
    bool force_rtc_sync = false;

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
      policy = gh_find_policy_for_slave(s_topology_policy_cache, policy_count, slave_id);
      force_rtc_sync = ((!s_slave_comm_up[slave_idx]) || (comm_fail_streak[slave_idx] > 0U));
      comm_fail_streak[slave_idx] = 0U;
      if (comm_ok_streak[slave_idx] < 0xFFU)
      {
        comm_ok_streak[slave_idx]++;
      }
      recover_cycles = gh_policy_recover_cycles(policy);
      quality = (comm_ok_streak[slave_idx] >= recover_cycles) ?
                  SENSOR_QUALITY_OK :
                  SENSOR_QUALITY_STALE;
      gh_set_quality_for_slave(slave_id, quality, s_topology_point_cache, point_count);
      if (policy != NULL)
      {
        s_last_policy_reason[slave_idx] = GH_POLICY_REASON_NONE;
        s_last_policy_action[slave_idx] = GH_TOPOLOGY_POLICY_ACTION_KEEP_LAST;
      }
      gh_maybe_sync_slave_rtc(slave_id, HAL_GetTick(), force_rtc_sync);
      if ((zone_slave_mask & bit) != 0U)
      {
        gh_maybe_sync_slave_weather(slave_id, HAL_GetTick(), force_rtc_sync);
      }
      s_slave_comm_up[slave_idx] = true;
    }
    else
    {
      policy = gh_find_policy_for_slave(s_topology_policy_cache, policy_count, slave_id);
      comm_ok_streak[slave_idx] = 0U;
      if (comm_fail_streak[slave_idx] < 0xFFU)
      {
        comm_fail_streak[slave_idx]++;
      }

      reason = slave_fail_reason[slave_idx];
      if (reason == GH_POLICY_REASON_NONE)
      {
        reason = GH_POLICY_REASON_TIMEOUT;
      }

      if (policy != NULL)
      {
        link_loss_threshold = gh_policy_link_loss_threshold(policy, fail_offline_cycles);
        if (comm_fail_streak[slave_idx] >= link_loss_threshold)
        {
          reason = GH_POLICY_REASON_LINK_LOSS;
        }
        action = gh_policy_action_for_reason(policy, reason);
        if (action != GH_TOPOLOGY_POLICY_ACTION_KEEP_LAST)
        {
          (void)gh_policy_apply_action(slave_id, action, s_topology_point_cache, point_count);
        }
        gh_policy_log_transition(slave_id,
                                 reason,
                                 action,
                                 comm_fail_streak[slave_idx],
                                 s_last_policy_reason,
                                 s_last_policy_action);
      }
      else
      {
        if (comm_fail_streak[slave_idx] >= fail_offline_cycles)
        {
          gh_set_quality_for_slave(slave_id, SENSOR_QUALITY_OFFLINE, s_topology_point_cache, point_count);
        }
        else if (comm_fail_streak[slave_idx] >= GH_QUALITY_FAIL_STALE_CYCLES)
        {
          gh_set_quality_for_slave(slave_id, SENSOR_QUALITY_STALE, s_topology_point_cache, point_count);
        }
      }

      if (slave_id <= (sizeof(g_status.modbus_timeouts) / sizeof(g_status.modbus_timeouts[0])))
      {
        g_status.modbus_timeouts[slave_id - 1U]++;
      }
      GH_ModbusMap_ReportTimeout(slave_id, HAL_GetTick());
      s_slave_comm_up[slave_idx] = false;
    }
  }

  (void)plan_slave_mask;
  gh_process_data_driven_command(s_topology_cmd_cache,
                                 cmd_count,
                                 s_topology_point_cache,
                                 point_count,
                                 true);
  return GH_TOPOLOGY_CYCLE_OK;
}

static void gh_run_safe_mode(uint8_t *comm_fail_streak,
                             uint8_t *comm_ok_streak,
                             uint8_t fail_offline_cycles,
                             gh_topology_cycle_state_t cycle_state,
                             bool publish_diag_event)
{
  uint8_t slave_id;
  uint8_t slave_idx;
  uint8_t quality = SENSOR_QUALITY_STALE;
  uint32_t now_ms = HAL_GetTick();
  uint16_t point_count = 0U;
  uint32_t point_generation = 0U;
  const gh_topology_point_binding_t *points = NULL;
  uint16_t diag_reason = (cycle_state == GH_TOPOLOGY_CYCLE_EMPTY) ? 2U : 1U;

  (void)GH_TopologyRuntime_CopyPointBindings(s_topology_point_cache,
                                             GH_TOPOLOGY_V2_MAX_POINTS,
                                             &point_count,
                                             &point_generation);
  if (point_count > 0U)
  {
    points = s_topology_point_cache;
  }

  if (publish_diag_event)
  {
    publish_event(EVENT_SEV_ALARM, EVENT_CODE_CTRL_SYNC_TOPOLOGY_CONTRACT, 0U, (float)diag_reason);
    g_status.last_error_code = EVENT_CODE_CTRL_SYNC_TOPOLOGY_CONTRACT;
  }

  for (slave_id = GH_RTU_SLAVE_FIRST; slave_id <= GH_RTU_SLAVE_LAST; slave_id++)
  {
    task_heartbeat_kick(TASK_BIT_MODBUS);
    if (!gh_slave_id_valid(slave_id))
    {
      continue;
    }
    slave_idx = gh_slave_to_index(slave_id);
    comm_ok_streak[slave_idx] = 0U;
    if (comm_fail_streak[slave_idx] < 0xFFU)
    {
      comm_fail_streak[slave_idx]++;
    }

    if (comm_fail_streak[slave_idx] >= fail_offline_cycles)
    {
      quality = SENSOR_QUALITY_OFFLINE;
    }
    else
    {
      quality = SENSOR_QUALITY_STALE;
    }
    gh_set_quality_for_slave(slave_id, quality, points, point_count);
    GH_ModbusMap_ReportTimeout(slave_id, now_ms);
    s_slave_comm_up[slave_idx] = false;
  }

  task_heartbeat_kick(TASK_BIT_MODBUS);
}

void GH_ModbusMasterTask_Run(void *argument)
{
  static uint8_t s_comm_fail_streak[MODBUS_MAX_SLAVES] = {0};
  static uint8_t s_comm_ok_streak[MODBUS_MAX_SLAVES] = {0};
  uint32_t cycle_start_ms = 0U;
  uint32_t cycle_elapsed_ms = 0U;
  uint8_t fail_offline_cycles = 0U;
  bool safe_mode_active = false;
  gh_topology_cycle_state_t safe_mode_reason = GH_TOPOLOGY_CYCLE_INVALID;
  gh_topology_cycle_state_t cycle_state = GH_TOPOLOGY_CYCLE_INVALID;
  (void)argument;

  fail_offline_cycles = gh_quality_fail_offline_cycles();
  gh_rtc_sync_publish_diag();

  for (;;)
  {
    cycle_start_ms = HAL_GetTick();
    task_heartbeat_kick(TASK_BIT_MODBUS);
    GH_ModbusMap_UpdateAges(cycle_start_ms);
    gh_apply_rtc_set_request();
    gh_refresh_rtc_clock_registers();

    cycle_state = gh_run_topology_cycle(s_comm_fail_streak, s_comm_ok_streak, fail_offline_cycles);
    if (cycle_state == GH_TOPOLOGY_CYCLE_OK)
    {
      safe_mode_active = false;
    }
    else
    {
      gh_run_safe_mode(s_comm_fail_streak,
                       s_comm_ok_streak,
                       fail_offline_cycles,
                       cycle_state,
                       (!safe_mode_active || (safe_mode_reason != cycle_state)));
      s_runtime_cmd_count = 0U;
      s_runtime_point_count = 0U;
      s_runtime_topology_ready = false;
      gh_process_data_driven_command(NULL, 0U, NULL, 0U, false);
      safe_mode_active = true;
      safe_mode_reason = cycle_state;
    }

    task_heartbeat_kick(TASK_BIT_MODBUS);
    cycle_elapsed_ms = HAL_GetTick() - cycle_start_ms;
    if (cycle_elapsed_ms < MODBUS_POLL_PERIOD_MS)
    {
      gh_service_pending_commands_during_idle(MODBUS_POLL_PERIOD_MS - cycle_elapsed_ms);
    }
    else
    {
      osDelay(MODBUS_INTER_SLAVE_DELAY_MS);
    }
  }
}
