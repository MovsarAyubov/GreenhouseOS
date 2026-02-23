#include "gh_modbus_map.h"

#include <string.h>

enum
{
  REG_OFF_SENS_0 = 0U,
  REG_OFF_SENS_VALID_MASK = 9U,
  REG_OFF_OUT_STATE_MASK = 10U,
  REG_OFF_OUT_CMD_MASK = 11U,
  REG_OFF_SLAVE_STATUS = 12U,
  REG_OFF_LAST_OK_AGE_SEC = 13U,
  REG_OFF_ERR_TIMEOUT = 14U,
  REG_OFF_ERR_CRC = 15U,
  REG_OFF_ERR_EXCEPTION = 16U,
  REG_OFF_DATA_VERSION = 17U,
  REG_OFF_MODE = 20U,
  REG_OFF_SET_TEMP_X10 = 21U,
  REG_OFF_SET_HUM_X10 = 22U,
  REG_OFF_HYST_TEMP_X10 = 23U,
  REG_OFF_HYST_HUM_X10 = 24U,
  REG_OFF_MIN_ON_SEC = 25U,
  REG_OFF_MIN_OFF_SEC = 26U,
  REG_OFF_APPLY_TRIGGER = 60U,
  REG_OFF_LAST_APPLIED_TRIGGER = 61U
};

enum
{
  CFG_OFF_SUBMIT_TOKEN = 0U,
  CFG_OFF_RESULT_CODE = 1U,
  CFG_OFF_RESULT_TOKEN = 2U,
  CFG_OFF_ACTIVE_VER_HI = 3U,
  CFG_OFF_ACTIVE_VER_LO = 4U,
  CFG_OFF_LAST_REQ_VER_HI = 5U,
  CFG_OFF_LAST_REQ_VER_LO = 6U,
  CFG_OFF_LAST_REQ_CRC_HI = 7U,
  CFG_OFF_LAST_REQ_CRC_LO = 8U,
  CFG_OFF_REQ_VER_HI = 10U,
  CFG_OFF_REQ_VER_LO = 11U,
  CFG_OFF_REQ_CRC_HI = 12U,
  CFG_OFF_REQ_CRC_LO = 13U,
  CFG_OFF_PAYLOAD_BASE = 16U,
  CFG_PAYLOAD_WORDS = (CONFIG_PAYLOAD_SIZE / 2U)
};

static uint16_t s_holding[GH_MB_TOTAL_REGS];
static uint32_t s_last_ok_ms[GH_MB_MAX_SLAVES];
static uint16_t s_last_submit_token = 0U;

static bool addr_to_index(uint16_t addr, uint16_t qty, uint16_t *out_idx)
{
  uint32_t idx;

  if ((qty == 0U) || (out_idx == NULL))
  {
    return false;
  }

  if (addr < GH_MB_TOTAL_REGS)
  {
    idx = (uint32_t)addr;
  }
  else if (addr >= GH_MB_HOLDING_BASE)
  {
    idx = (uint32_t)(addr - GH_MB_HOLDING_BASE);
  }
  else
  {
    return false;
  }

  if ((idx + qty) > GH_MB_TOTAL_REGS)
  {
    return false;
  }

  *out_idx = (uint16_t)idx;
  return true;
}

static bool slave_to_base(uint8_t slave_id, uint16_t *out_base)
{
  uint16_t base;
  if ((slave_id == 0U) || (slave_id > GH_MB_MAX_SLAVES) || (out_base == NULL))
  {
    return false;
  }
  base = (uint16_t)(((uint16_t)(slave_id - 1U)) * GH_MB_BLOCK_SIZE);
  *out_base = base;
  return true;
}

static void bump_data_version(uint16_t base)
{
  s_holding[base + REG_OFF_DATA_VERSION]++;
}

static uint16_t cfg_index(uint16_t off)
{
  return (uint16_t)(GH_MB_CFG_BASE + off);
}

static void cfg_set_u32(uint16_t off_hi, uint16_t off_lo, uint32_t value)
{
  s_holding[cfg_index(off_hi)] = (uint16_t)((value >> 16U) & 0xFFFFU);
  s_holding[cfg_index(off_lo)] = (uint16_t)(value & 0xFFFFU);
}

static uint32_t cfg_get_u32(uint16_t off_hi, uint16_t off_lo)
{
  return ((uint32_t)s_holding[cfg_index(off_hi)] << 16U) |
         (uint32_t)s_holding[cfg_index(off_lo)];
}

void GH_ModbusMap_ReportConfigResult(uint16_t token,
                                     config_result_code_t result,
                                     uint32_t active_version)
{
  s_holding[cfg_index(CFG_OFF_RESULT_CODE)] = (uint16_t)result;
  s_holding[cfg_index(CFG_OFF_RESULT_TOKEN)] = token;
  cfg_set_u32(CFG_OFF_ACTIVE_VER_HI, CFG_OFF_ACTIVE_VER_LO, active_version);
}

static bool cfg_try_submit(uint16_t token)
{
  config_update_req_t req = {0};
  uint16_t i;

  req.request_token = token;
  req.version = cfg_get_u32(CFG_OFF_REQ_VER_HI, CFG_OFF_REQ_VER_LO);
  req.payload_crc = cfg_get_u32(CFG_OFF_REQ_CRC_HI, CFG_OFF_REQ_CRC_LO);

  for (i = 0U; i < CFG_PAYLOAD_WORDS; i++)
  {
    uint16_t reg = s_holding[cfg_index((uint16_t)(CFG_OFF_PAYLOAD_BASE + i))];
    req.payload[2U * i] = (uint8_t)(reg >> 8U);
    req.payload[(2U * i) + 1U] = (uint8_t)(reg & 0xFFU);
  }

  if ((osKernelGetState() != osKernelRunning) || (qConfigStoreHandle == NULL))
  {
    g_setpoints_apply_in_progress = false;
    GH_ModbusMap_ReportConfigResult(token, CFG_RESULT_REJECT_QUEUE_FULL, g_active_config.version);
    publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_REJECT_QUEUE_FULL);
    return false;
  }

  cfg_set_u32(CFG_OFF_LAST_REQ_VER_HI, CFG_OFF_LAST_REQ_VER_LO, req.version);
  cfg_set_u32(CFG_OFF_LAST_REQ_CRC_HI, CFG_OFF_LAST_REQ_CRC_LO, req.payload_crc);

  if (osMessageQueuePut(qConfigStoreHandle, &req, 0U, 0U) != osOK)
  {
    g_setpoints_apply_in_progress = false;
    GH_ModbusMap_ReportConfigResult(token, CFG_RESULT_REJECT_QUEUE_FULL, g_active_config.version);
    publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_REJECT_QUEUE_FULL);
    return false;
  }

  g_setpoints_apply_in_progress = true;
  GH_ModbusMap_ReportConfigResult(token, CFG_RESULT_QUEUED, g_active_config.version);
  s_last_submit_token = token;
  return true;
}

static void cfg_maybe_submit_after_write(uint16_t start_idx, uint16_t qty)
{
  uint16_t submit_idx = cfg_index(CFG_OFF_SUBMIT_TOKEN);
  uint16_t token;

  if ((start_idx > submit_idx) || ((start_idx + qty) <= submit_idx))
  {
    return;
  }

  token = s_holding[submit_idx];
  if ((token == 0U) || (token == s_last_submit_token))
  {
    return;
  }

  (void)cfg_try_submit(token);
}

void GH_ModbusMap_Init(void)
{
  uint8_t s;
  uint16_t base;

  memset(s_holding, 0, sizeof(s_holding));
  memset(s_last_ok_ms, 0, sizeof(s_last_ok_ms));
  s_last_submit_token = 0U;

  for (s = 1U; s <= GH_MB_MAX_SLAVES; s++)
  {
    if (!slave_to_base(s, &base))
    {
      continue;
    }
    s_holding[base + REG_OFF_MODE] = 0U;
    s_holding[base + REG_OFF_SLAVE_STATUS] = 0x0002U; /* stale=1 at startup */
  }

  s_holding[cfg_index(CFG_OFF_RESULT_CODE)] = (uint16_t)CFG_RESULT_IDLE;
  s_holding[cfg_index(CFG_OFF_RESULT_TOKEN)] = 0U;
  cfg_set_u32(CFG_OFF_ACTIVE_VER_HI, CFG_OFF_ACTIVE_VER_LO, g_active_config.version);
  s_holding[cfg_index(CFG_OFF_SUBMIT_TOKEN)] = 0U;
}

void GH_ModbusMap_UpdateAges(uint32_t now_ms)
{
  uint8_t s;
  uint16_t base;
  uint32_t age_sec;

  for (s = 1U; s <= GH_MB_MAX_SLAVES; s++)
  {
    if (!slave_to_base(s, &base))
    {
      continue;
    }

    if (s_last_ok_ms[s - 1U] == 0U)
    {
      s_holding[base + REG_OFF_LAST_OK_AGE_SEC] = 0xFFFFU;
      continue;
    }

    age_sec = (now_ms - s_last_ok_ms[s - 1U]) / 1000U;
    if (age_sec > 0xFFFFU)
    {
      age_sec = 0xFFFFU;
    }
    s_holding[base + REG_OFF_LAST_OK_AGE_SEC] = (uint16_t)age_sec;
  }
}

bool GH_ModbusMap_ReadRange(uint16_t start_addr, uint16_t qty, uint16_t *out_regs)
{
  uint16_t idx;

  if ((out_regs == NULL) || !addr_to_index(start_addr, qty, &idx))
  {
    return false;
  }

  memcpy(out_regs, &s_holding[idx], (uint32_t)qty * sizeof(uint16_t));
  return true;
}

bool GH_ModbusMap_WriteSingle(uint16_t addr, uint16_t value)
{
  uint16_t idx;
  if (!addr_to_index(addr, 1U, &idx))
  {
    return false;
  }
  s_holding[idx] = value;
  cfg_maybe_submit_after_write(idx, 1U);
  return true;
}

bool GH_ModbusMap_WriteRange(uint16_t start_addr, uint16_t qty, const uint16_t *values)
{
  uint16_t idx;

  if ((values == NULL) || !addr_to_index(start_addr, qty, &idx))
  {
    return false;
  }

  memcpy(&s_holding[idx], values, (uint32_t)qty * sizeof(uint16_t));
  cfg_maybe_submit_after_write(idx, qty);
  return true;
}

void GH_ModbusMap_UpdateTelemetry(uint8_t slave_id,
                                  const uint16_t *sensors_9,
                                  uint16_t valid_mask,
                                  uint16_t out_state_mask,
                                  uint32_t now_ms)
{
  uint16_t base;
  uint8_t i;

  if ((sensors_9 == NULL) || !slave_to_base(slave_id, &base))
  {
    return;
  }

  for (i = 0U; i < 9U; i++)
  {
    s_holding[base + REG_OFF_SENS_0 + i] = sensors_9[i];
  }
  s_holding[base + REG_OFF_SENS_VALID_MASK] = valid_mask;
  s_holding[base + REG_OFF_OUT_STATE_MASK] = out_state_mask;
  s_holding[base + REG_OFF_SLAVE_STATUS] = 0x0001U; /* online=1, stale=0 */
  s_holding[base + REG_OFF_LAST_OK_AGE_SEC] = 0U;
  s_last_ok_ms[slave_id - 1U] = now_ms;
  bump_data_version(base);
}

void GH_ModbusMap_ReportTimeout(uint8_t slave_id, uint32_t now_ms)
{
  uint16_t base;
  (void)now_ms;

  if (!slave_to_base(slave_id, &base))
  {
    return;
  }
  s_holding[base + REG_OFF_ERR_TIMEOUT]++;
  s_holding[base + REG_OFF_SLAVE_STATUS] = 0x0002U; /* online=0, stale=1 */
}

void GH_ModbusMap_UpdateDiag(uint8_t slave_id,
                             uint16_t err_timeout,
                             uint16_t err_crc,
                             uint16_t err_exception)
{
  uint16_t base;
  if (!slave_to_base(slave_id, &base))
  {
    return;
  }
  s_holding[base + REG_OFF_ERR_TIMEOUT] = err_timeout;
  s_holding[base + REG_OFF_ERR_CRC] = err_crc;
  s_holding[base + REG_OFF_ERR_EXCEPTION] = err_exception;
}

bool GH_ModbusMap_GetApplyRequest(uint8_t slave_id, gh_slave_apply_request_t *out_req)
{
  uint16_t base;
  uint16_t trigger;
  uint16_t applied;

  if ((out_req == NULL) || !slave_to_base(slave_id, &base))
  {
    return false;
  }

  trigger = s_holding[base + REG_OFF_APPLY_TRIGGER];
  applied = s_holding[base + REG_OFF_LAST_APPLIED_TRIGGER];
  if (trigger == applied)
  {
    return false;
  }

  out_req->trigger = trigger;
  out_req->setpoints[0] = s_holding[base + REG_OFF_MODE];
  out_req->setpoints[1] = s_holding[base + REG_OFF_SET_TEMP_X10];
  out_req->setpoints[2] = s_holding[base + REG_OFF_SET_HUM_X10];
  out_req->setpoints[3] = s_holding[base + REG_OFF_HYST_TEMP_X10];
  out_req->setpoints[4] = s_holding[base + REG_OFF_HYST_HUM_X10];
  out_req->setpoints[5] = s_holding[base + REG_OFF_MIN_ON_SEC];
  out_req->setpoints[6] = s_holding[base + REG_OFF_MIN_OFF_SEC];
  out_req->out_cmd_mask = s_holding[base + REG_OFF_OUT_CMD_MASK];

  return true;
}

void GH_ModbusMap_MarkApplyResult(uint8_t slave_id, uint16_t trigger, bool applied)
{
  uint16_t base;
  if (!slave_to_base(slave_id, &base))
  {
    return;
  }
  if (applied)
  {
    s_holding[base + REG_OFF_LAST_APPLIED_TRIGGER] = trigger;
    bump_data_version(base);
  }
}

uint16_t *GH_ModbusMap_GetBackingStore(void)
{
  return s_holding;
}

uint16_t GH_ModbusMap_GetBackingStoreSize(void)
{
  return GH_MB_TOTAL_REGS;
}
