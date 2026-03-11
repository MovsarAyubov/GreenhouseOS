#include "gh_modbus_map.h"
#include "gh_topology_runtime.h"

#include <string.h>

#ifdef GH_USE_LWIP_NETCONN
typedef struct
{
  uint32_t acceptErrCount;
  uint32_t recvTimeoutCount;
  uint32_t recvClosedCount;
  uint32_t recvOtherErrCount;
  uint32_t staleCloseCount;
  uint32_t malformedMbapCount;
  uint32_t sendErrCount;
  int32_t lastRecvErr;
  int32_t lastSendErr;
} modbusTcpDiag_t;
void ModbusTcpGetDiag(modbusTcpDiag_t *diagOut);
#endif

enum
{
  PNT_OFF_VALUE_HI = 0U,
  PNT_OFF_VALUE_LO = 1U,
  PNT_OFF_QUALITY = 2U,
  PNT_OFF_AGE_SEC = 3U,
  PNT_OFF_MODULE_ID = 4U,
  PNT_OFF_FLAGS = 5U
};

enum
{
  ST_OFF_STATUS = 0U,
  ST_OFF_LAST_OK_AGE_SEC = 1U,
  ST_OFF_ERR_TIMEOUT = 2U,
  ST_OFF_ERR_CRC = 3U,
  ST_OFF_ERR_EXCEPTION = 4U,
  ST_OFF_DATA_VERSION = 5U,
  ST_OFF_VALID_MASK = 6U,
  ST_OFF_OUT_STATE_MASK = 7U
};

enum
{
  CMD_OFF_TARGET_SLAVE_ID = 0U,
  CMD_OFF_TARGET_MODULE_ID = 1U,
  CMD_OFF_CMD_PROFILE_ID = 2U,
  CMD_OFF_PAYLOAD_LEN = 3U,
  CMD_OFF_PAYLOAD_BASE = 4U,
  CMD_OFF_TRIGGER = CMD_OFF_PAYLOAD_BASE + GH_MB_CMD_PAYLOAD_WORDS,
  CMD_OFF_LAST_APPLIED_TRIGGER = CMD_OFF_TRIGGER + 1U,
  CMD_OFF_RESULT = CMD_OFF_TRIGGER + 2U,
  CMD_OFF_IO_ERR = CMD_OFF_TRIGGER + 3U
};

enum
{
  DIR_OFF_MAP_VERSION = 0U,
  DIR_OFF_MAP_FLAGS = 1U,
  DIR_OFF_TOPO_GEN_HI = 2U,
  DIR_OFF_TOPO_GEN_LO = 3U,
  DIR_OFF_POINT_COUNT = 4U,
  DIR_OFF_POINT_STRIDE = 5U,
  DIR_OFF_POINTS_BASE = 6U,
  DIR_OFF_SLAVE_STATUS_BASE = 7U,
  DIR_OFF_CMD_BASE = 8U,
  DIR_OFF_DATA_VER_HI = 9U,
  DIR_OFF_DATA_VER_LO = 10U,
  DIR_OFF_MAX_POINTS = 11U,
  DIR_OFF_CMD_BLOCK_SIZE = 12U,
  DIR_OFF_STATUS_BLOCK_SIZE = 13U,
  DIR_OFF_RTC_HOUR = GH_MB_DIR_OFF_RTC_HOUR,
  DIR_OFF_RTC_MINUTE = GH_MB_DIR_OFF_RTC_MINUTE,
  DIR_OFF_RTC_SET_HOUR = GH_MB_DIR_OFF_RTC_SET_HOUR,
  DIR_OFF_RTC_SET_MINUTE = GH_MB_DIR_OFF_RTC_SET_MINUTE,
  DIR_OFF_RTC_SET_TOKEN = GH_MB_DIR_OFF_RTC_SET_TOKEN,
  DIR_OFF_RTC_SET_APPLIED_TOKEN = GH_MB_DIR_OFF_RTC_SET_APPLIED_TOKEN,
  DIR_OFF_RTC_SET_RESULT = GH_MB_DIR_OFF_RTC_SET_RESULT,
  DIR_OFF_RTC_SYNC_ATTEMPT_HI = GH_MB_DIR_OFF_RTC_SYNC_ATTEMPT_HI,
  DIR_OFF_RTC_SYNC_ATTEMPT_LO = GH_MB_DIR_OFF_RTC_SYNC_ATTEMPT_LO,
  DIR_OFF_RTC_SYNC_OK_HI = GH_MB_DIR_OFF_RTC_SYNC_OK_HI,
  DIR_OFF_RTC_SYNC_OK_LO = GH_MB_DIR_OFF_RTC_SYNC_OK_LO,
  DIR_OFF_RTC_SYNC_FAIL_HI = GH_MB_DIR_OFF_RTC_SYNC_FAIL_HI,
  DIR_OFF_RTC_SYNC_FAIL_LO = GH_MB_DIR_OFF_RTC_SYNC_FAIL_LO,
  DIR_OFF_RTC_SYNC_LAST_SLAVE = GH_MB_DIR_OFF_RTC_SYNC_LAST_SLAVE,
  DIR_OFF_RTC_SYNC_LAST_TOKEN = GH_MB_DIR_OFF_RTC_SYNC_LAST_TOKEN,
  DIR_OFF_RTC_SYNC_LAST_RESULT = GH_MB_DIR_OFF_RTC_SYNC_LAST_RESULT
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

enum
{
  DBG_OFF_BOOT_COUNT_HI = 0U,
  DBG_OFF_BOOT_COUNT_LO = 1U,
  DBG_OFF_POWERON_COUNT_HI = 2U,
  DBG_OFF_POWERON_COUNT_LO = 3U,
  DBG_OFF_ERR_HANDLER_COUNT_HI = 4U,
  DBG_OFF_ERR_HANDLER_COUNT_LO = 5U,
  DBG_OFF_WDG_MISS_COUNT_HI = 6U,
  DBG_OFF_WDG_MISS_COUNT_LO = 7U,
  DBG_OFF_FAULT_RESET_COUNT_HI = 8U,
  DBG_OFF_FAULT_RESET_COUNT_LO = 9U,
  DBG_OFF_LAST_EVENT_CODE_HI = 10U,
  DBG_OFF_LAST_EVENT_CODE_LO = 11U,
  DBG_OFF_LAST_RESET_REASON_HI = 12U,
  DBG_OFF_LAST_RESET_REASON_LO = 13U,
  DBG_OFF_LAST_ERROR_CODE_HI = 14U,
  DBG_OFF_LAST_ERROR_CODE_LO = 15U,
  DBG_OFF_MODBUS_TIMEOUT0_HI = 16U,
  DBG_OFF_MODBUS_TIMEOUT0_LO = 17U,
  DBG_OFF_MODBUS_TIMEOUT1_HI = 18U,
  DBG_OFF_MODBUS_TIMEOUT1_LO = 19U,
  DBG_OFF_TCP_ACCEPT_ERR_HI = 20U,
  DBG_OFF_TCP_ACCEPT_ERR_LO = 21U,
  DBG_OFF_TCP_RECV_TIMEOUT_HI = 22U,
  DBG_OFF_TCP_RECV_TIMEOUT_LO = 23U,
  DBG_OFF_TCP_STALE_CLOSE_HI = 24U,
  DBG_OFF_TCP_STALE_CLOSE_LO = 25U,
  DBG_OFF_TCP_MALFORMED_MBAP_HI = 26U,
  DBG_OFF_TCP_MALFORMED_MBAP_LO = 27U,
  DBG_OFF_TCP_SEND_ERR_HI = 28U,
  DBG_OFF_TCP_SEND_ERR_LO = 29U,
  DBG_OFF_TCP_LAST_ERR_HI = 30U,
  DBG_OFF_TCP_LAST_ERR_LO = 31U
};

enum
{
  TOPO_OFF_SUBMIT_TOKEN = 0U,
  TOPO_OFF_RESULT_CODE = 1U,
  TOPO_OFF_RESULT_TOKEN = 2U,
  TOPO_OFF_ACTIVE_FLAGS = 3U,
  TOPO_OFF_ACTIVE_VER_MAJOR = 4U,
  TOPO_OFF_ACTIVE_VER_MINOR = 5U,
  TOPO_OFF_ACTIVE_GEN_HI = 6U,
  TOPO_OFF_ACTIVE_GEN_LO = 7U,
  TOPO_OFF_ACTIVE_SIZE_HI = 8U,
  TOPO_OFF_ACTIVE_SIZE_LO = 9U,
  TOPO_OFF_REQ_CHUNK_INDEX = 10U,
  TOPO_OFF_REQ_CHUNK_WORDS = 11U,
  TOPO_OFF_REQ_TOTAL_SIZE_HI = 12U,
  TOPO_OFF_REQ_TOTAL_SIZE_LO = 13U,
  TOPO_OFF_REQ_CHUNK_CRC_HI = 14U,
  TOPO_OFF_REQ_CHUNK_CRC_LO = 15U,
  TOPO_OFF_REQ_FLAGS = 16U,
  TOPO_OFF_REQ_GEN_HI = 17U,
  TOPO_OFF_REQ_GEN_LO = 18U,
  TOPO_OFF_CHUNK_BASE = 20U
};

static uint16_t s_holding[GH_MB_TOTAL_REGS];
static uint32_t s_last_ok_ms[GH_MB_MAX_SLAVES];
static uint16_t s_point_age_sec[GH_MB_POINT_MAX];
static uint16_t s_point_module_id[GH_MB_POINT_MAX];
static gh_topology_point_binding_t s_point_bindings_cache[GH_MB_POINT_MAX] __attribute__((section(".ccmram")));
static uint16_t s_last_submit_token = 0U;
static uint16_t s_last_topo_submit_token = 0U;
static uint16_t s_last_rtc_submit_token = 0U;
static uint16_t s_last_cmd_submit_trigger = 0U;
static gh_rtc_set_request_t s_rtc_set_request = {0};
static gh_data_driven_command_request_t s_cmd_pending_request = {0};
static bool s_cmd_pending = false;
static bool s_rtc_set_pending = false;
static uint32_t s_point_module_generation = 0U;
static uint32_t s_map_data_version = 0U;
static osMutexId_t s_map_mutex = NULL;

static bool map_ensure_mutex(void)
{
  if (s_map_mutex != NULL)
  {
    return true;
  }

  if (osKernelGetState() != osKernelRunning)
  {
    return true;
  }

  s_map_mutex = osMutexNew(NULL);
  return (s_map_mutex != NULL);
}

static bool map_lock(void)
{
  if (!map_ensure_mutex())
  {
    return false;
  }

  if (s_map_mutex == NULL)
  {
    return true;
  }

  return (osMutexAcquire(s_map_mutex, osWaitForever) == osOK);
}

static void map_unlock(void)
{
  if (s_map_mutex != NULL)
  {
    (void)osMutexRelease(s_map_mutex);
  }
}

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

static bool slave_status_base(uint8_t slave_id, uint16_t *out_base)
{
  if ((slave_id == 0U) || (slave_id > GH_MB_MAX_SLAVES) || (out_base == NULL))
  {
    return false;
  }
  *out_base = (uint16_t)(GH_MB_SLAVE_STATUS_BASE +
                         ((uint16_t)(slave_id - 1U) * GH_MB_SLAVE_STATUS_BLOCK_SIZE));
  return true;
}

static bool point_base(uint16_t publish_index, uint16_t *out_base)
{
  if ((publish_index >= GH_MB_POINT_MAX) || (out_base == NULL))
  {
    return false;
  }
  *out_base = (uint16_t)(GH_MB_POINTS_BASE + (publish_index * GH_MB_POINT_STRIDE));
  return true;
}

static void bump_slave_data_version_nolock(uint16_t status_base)
{
  s_holding[status_base + ST_OFF_DATA_VERSION]++;
}

static uint16_t cfg_index(uint16_t off)
{
  return (uint16_t)(GH_MB_CFG_BASE + off);
}

static uint16_t dbg_index(uint16_t off)
{
  return (uint16_t)(GH_MB_DIAG_BASE + off);
}

static uint16_t topo_index(uint16_t off)
{
  return (uint16_t)(GH_MB_TOPO_BASE + off);
}

static uint16_t dir_index(uint16_t off)
{
  return (uint16_t)(GH_MB_DIR_BASE + off);
}

static void cfg_set_u32(uint16_t off_hi, uint16_t off_lo, uint32_t value)
{
  s_holding[cfg_index(off_hi)] = (uint16_t)((value >> 16U) & 0xFFFFU);
  s_holding[cfg_index(off_lo)] = (uint16_t)(value & 0xFFFFU);
}

static void dbg_set_u32(uint16_t off_hi, uint16_t off_lo, uint32_t value)
{
  s_holding[dbg_index(off_hi)] = (uint16_t)((value >> 16U) & 0xFFFFU);
  s_holding[dbg_index(off_lo)] = (uint16_t)(value & 0xFFFFU);
}

static void topo_set_u32(uint16_t off_hi, uint16_t off_lo, uint32_t value)
{
  s_holding[topo_index(off_hi)] = (uint16_t)((value >> 16U) & 0xFFFFU);
  s_holding[topo_index(off_lo)] = (uint16_t)(value & 0xFFFFU);
}

static void dir_set_u32(uint16_t off_hi, uint16_t off_lo, uint32_t value)
{
  s_holding[dir_index(off_hi)] = (uint16_t)((value >> 16U) & 0xFFFFU);
  s_holding[dir_index(off_lo)] = (uint16_t)(value & 0xFFFFU);
}

static uint32_t cfg_get_u32(uint16_t off_hi, uint16_t off_lo)
{
  return ((uint32_t)s_holding[cfg_index(off_hi)] << 16U) |
         (uint32_t)s_holding[cfg_index(off_lo)];
}

static uint32_t topo_get_u32(uint16_t off_hi, uint16_t off_lo)
{
  return ((uint32_t)s_holding[topo_index(off_hi)] << 16U) |
         (uint32_t)s_holding[topo_index(off_lo)];
}

static void map_refresh_point_modules_nolock(void)
{
  uint16_t count = 0U;
  uint32_t generation = 0U;
  uint16_t i;

  if ((g_topology_v2_active == 0U) || (g_topology_v2_generation == 0U))
  {
    if (s_point_module_generation != 0U)
    {
      memset(s_point_module_id, 0, sizeof(s_point_module_id));
      s_point_module_generation = 0U;
    }
    return;
  }

  if (s_point_module_generation == g_topology_v2_generation)
  {
    return;
  }

  memset(s_point_module_id, 0, sizeof(s_point_module_id));
  if (!GH_TopologyRuntime_CopyPointBindings(s_point_bindings_cache,
                                            GH_MB_POINT_MAX,
                                            &count,
                                            &generation))
  {
    s_point_module_generation = 0U;
    return;
  }
  if (generation != g_topology_v2_generation)
  {
    s_point_module_generation = 0U;
    return;
  }

  for (i = 0U; i < count; i++)
  {
    if (s_point_bindings_cache[i].publish_index < GH_MB_POINT_MAX)
    {
      s_point_module_id[s_point_bindings_cache[i].publish_index] = s_point_bindings_cache[i].module_id;
    }
  }

  s_point_module_generation = generation;
}

static void map_refresh_points_nolock(void)
{
  uint16_t i;
  uint16_t base;
  uint32_t raw_value;
  uint16_t point_count;

  map_refresh_point_modules_nolock();

  point_count = g_topology_v2_point_count;
  if (point_count > GH_MB_POINT_MAX)
  {
    point_count = GH_MB_POINT_MAX;
  }

  for (i = 0U; i < GH_MB_POINT_MAX; i++)
  {
    if (!point_base(i, &base))
    {
      continue;
    }

    if ((i < point_count) && (s_point_module_id[i] != 0U))
    {
      memcpy(&raw_value, &g_sensors[i].value, sizeof(raw_value));
      s_holding[base + PNT_OFF_VALUE_HI] = (uint16_t)((raw_value >> 16U) & 0xFFFFU);
      s_holding[base + PNT_OFF_VALUE_LO] = (uint16_t)(raw_value & 0xFFFFU);
      s_holding[base + PNT_OFF_QUALITY] = g_sensors[i].quality;
      s_holding[base + PNT_OFF_AGE_SEC] = s_point_age_sec[i];
      s_holding[base + PNT_OFF_MODULE_ID] = s_point_module_id[i];
      s_holding[base + PNT_OFF_FLAGS] = 0x0001U;
    }
    else
    {
      s_holding[base + PNT_OFF_VALUE_HI] = 0U;
      s_holding[base + PNT_OFF_VALUE_LO] = 0U;
      s_holding[base + PNT_OFF_QUALITY] = SENSOR_QUALITY_OFFLINE;
      s_holding[base + PNT_OFF_AGE_SEC] = 0xFFFFU;
      s_holding[base + PNT_OFF_MODULE_ID] = 0U;
      s_holding[base + PNT_OFF_FLAGS] = 0U;
    }
  }
}

static void map_refresh_directory_nolock(void)
{
  uint16_t point_count = g_topology_v2_point_count;
  uint16_t flags = 0x0001U;

  if (point_count > GH_MB_POINT_MAX)
  {
    point_count = GH_MB_POINT_MAX;
  }
  if (g_topology_v2_active != 0U)
  {
    flags |= 0x0002U;
  }

  s_holding[dir_index(DIR_OFF_MAP_VERSION)] = GH_MB_MAP_VERSION;
  s_holding[dir_index(DIR_OFF_MAP_FLAGS)] = flags;
  s_holding[dir_index(DIR_OFF_TOPO_GEN_HI)] = (uint16_t)((g_topology_v2_generation >> 16U) & 0xFFFFU);
  s_holding[dir_index(DIR_OFF_TOPO_GEN_LO)] = (uint16_t)(g_topology_v2_generation & 0xFFFFU);
  s_holding[dir_index(DIR_OFF_POINT_COUNT)] = point_count;
  s_holding[dir_index(DIR_OFF_POINT_STRIDE)] = GH_MB_POINT_STRIDE;
  s_holding[dir_index(DIR_OFF_POINTS_BASE)] = GH_MB_POINTS_BASE;
  s_holding[dir_index(DIR_OFF_SLAVE_STATUS_BASE)] = GH_MB_SLAVE_STATUS_BASE;
  s_holding[dir_index(DIR_OFF_CMD_BASE)] = GH_MB_CMD_BASE;
  dir_set_u32(DIR_OFF_DATA_VER_HI, DIR_OFF_DATA_VER_LO, s_map_data_version);
  s_holding[dir_index(DIR_OFF_MAX_POINTS)] = GH_MB_POINT_MAX;
  s_holding[dir_index(DIR_OFF_CMD_BLOCK_SIZE)] = GH_MB_CMD_BLOCK_SIZE;
  s_holding[dir_index(DIR_OFF_STATUS_BLOCK_SIZE)] = GH_MB_SLAVE_STATUS_BLOCK_SIZE;
}

static void map_refresh_runtime_diag_nolock(void)
{
  uint32_t tcp_accept_err = 0U;
  uint32_t tcp_recv_timeout = 0U;
  uint32_t tcp_stale_close = 0U;
  uint32_t tcp_malformed = 0U;
  uint32_t tcp_send_err = 0U;
  int32_t tcp_last_err = 0;

  dbg_set_u32(DBG_OFF_BOOT_COUNT_HI, DBG_OFF_BOOT_COUNT_LO, g_persist_boot_count);
  dbg_set_u32(DBG_OFF_POWERON_COUNT_HI, DBG_OFF_POWERON_COUNT_LO, g_persist_poweron_count);
  dbg_set_u32(DBG_OFF_ERR_HANDLER_COUNT_HI, DBG_OFF_ERR_HANDLER_COUNT_LO, g_persist_error_handler_count);
  dbg_set_u32(DBG_OFF_WDG_MISS_COUNT_HI, DBG_OFF_WDG_MISS_COUNT_LO, g_persist_wdg_miss_count);
  dbg_set_u32(DBG_OFF_FAULT_RESET_COUNT_HI, DBG_OFF_FAULT_RESET_COUNT_LO, g_persist_fault_reset_count);
  dbg_set_u32(DBG_OFF_LAST_EVENT_CODE_HI, DBG_OFF_LAST_EVENT_CODE_LO, g_persist_last_event_code);
  dbg_set_u32(DBG_OFF_LAST_RESET_REASON_HI, DBG_OFF_LAST_RESET_REASON_LO, g_persist_last_reset_reason);
  dbg_set_u32(DBG_OFF_LAST_ERROR_CODE_HI, DBG_OFF_LAST_ERROR_CODE_LO, g_status.last_error_code);
  dbg_set_u32(DBG_OFF_MODBUS_TIMEOUT0_HI, DBG_OFF_MODBUS_TIMEOUT0_LO, g_status.modbus_timeouts[0]);
  dbg_set_u32(DBG_OFF_MODBUS_TIMEOUT1_HI, DBG_OFF_MODBUS_TIMEOUT1_LO, g_status.modbus_timeouts[1]);

#ifdef GH_USE_LWIP_NETCONN
  modbusTcpDiag_t tcp_diag = {0};
  ModbusTcpGetDiag(&tcp_diag);
  tcp_accept_err = tcp_diag.acceptErrCount;
  tcp_recv_timeout = tcp_diag.recvTimeoutCount;
  tcp_stale_close = tcp_diag.staleCloseCount;
  tcp_malformed = tcp_diag.malformedMbapCount;
  tcp_send_err = tcp_diag.sendErrCount;
  tcp_last_err = (tcp_diag.lastRecvErr != 0) ? tcp_diag.lastRecvErr : tcp_diag.lastSendErr;
#endif
  dbg_set_u32(DBG_OFF_TCP_ACCEPT_ERR_HI, DBG_OFF_TCP_ACCEPT_ERR_LO, tcp_accept_err);
  dbg_set_u32(DBG_OFF_TCP_RECV_TIMEOUT_HI, DBG_OFF_TCP_RECV_TIMEOUT_LO, tcp_recv_timeout);
  dbg_set_u32(DBG_OFF_TCP_STALE_CLOSE_HI, DBG_OFF_TCP_STALE_CLOSE_LO, tcp_stale_close);
  dbg_set_u32(DBG_OFF_TCP_MALFORMED_MBAP_HI, DBG_OFF_TCP_MALFORMED_MBAP_LO, tcp_malformed);
  dbg_set_u32(DBG_OFF_TCP_SEND_ERR_HI, DBG_OFF_TCP_SEND_ERR_LO, tcp_send_err);
  dbg_set_u32(DBG_OFF_TCP_LAST_ERR_HI, DBG_OFF_TCP_LAST_ERR_LO, (uint32_t)tcp_last_err);

  s_holding[topo_index(TOPO_OFF_ACTIVE_FLAGS)] = ((g_topology_v2_active != 0U) ? 0x0001U : 0U) |
                                                 ((g_topology_commit_in_progress != 0U) ? 0x0002U : 0U);
  s_holding[topo_index(TOPO_OFF_ACTIVE_VER_MAJOR)] = g_topology_v2_ver_major;
  s_holding[topo_index(TOPO_OFF_ACTIVE_VER_MINOR)] = g_topology_v2_ver_minor;
  topo_set_u32(TOPO_OFF_ACTIVE_GEN_HI, TOPO_OFF_ACTIVE_GEN_LO, g_topology_v2_generation);
  topo_set_u32(TOPO_OFF_ACTIVE_SIZE_HI, TOPO_OFF_ACTIVE_SIZE_LO, g_topology_v2_active_size);

  map_refresh_directory_nolock();
  map_refresh_points_nolock();
}

static void cfg_report_result_nolock(uint16_t token,
                                     config_result_code_t result,
                                     uint32_t active_version)
{
  s_holding[cfg_index(CFG_OFF_RESULT_CODE)] = (uint16_t)result;
  s_holding[cfg_index(CFG_OFF_RESULT_TOKEN)] = token;
  cfg_set_u32(CFG_OFF_ACTIVE_VER_HI, CFG_OFF_ACTIVE_VER_LO, active_version);
}

static void topo_report_result_nolock(uint16_t token,
                                      config_result_code_t result,
                                      uint32_t generation,
                                      uint32_t active_size)
{
  s_holding[topo_index(TOPO_OFF_RESULT_CODE)] = (uint16_t)result;
  s_holding[topo_index(TOPO_OFF_RESULT_TOKEN)] = token;
  topo_set_u32(TOPO_OFF_ACTIVE_GEN_HI, TOPO_OFF_ACTIVE_GEN_LO, generation);
  topo_set_u32(TOPO_OFF_ACTIVE_SIZE_HI, TOPO_OFF_ACTIVE_SIZE_LO, active_size);
}

void GH_ModbusMap_ReportConfigResult(uint16_t token,
                                     config_result_code_t result,
                                     uint32_t active_version)
{
  if (!map_lock())
  {
    return;
  }
  cfg_report_result_nolock(token, result, active_version);
  map_unlock();
}

void GH_ModbusMap_ReportTopologyResult(uint16_t token,
                                       config_result_code_t result,
                                       uint32_t generation,
                                       uint32_t active_size)
{
  if (!map_lock())
  {
    return;
  }
  topo_report_result_nolock(token, result, generation, active_size);
  map_unlock();
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
    cfg_report_result_nolock(token, CFG_RESULT_REJECT_QUEUE_FULL, g_active_config.version);
    publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_REJECT_QUEUE_FULL);
    return false;
  }

  cfg_set_u32(CFG_OFF_LAST_REQ_VER_HI, CFG_OFF_LAST_REQ_VER_LO, req.version);
  cfg_set_u32(CFG_OFF_LAST_REQ_CRC_HI, CFG_OFF_LAST_REQ_CRC_LO, req.payload_crc);

  if (osMessageQueuePut(qConfigStoreHandle, &req, 0U, 0U) != osOK)
  {
    g_setpoints_apply_in_progress = false;
    cfg_report_result_nolock(token, CFG_RESULT_REJECT_QUEUE_FULL, g_active_config.version);
    publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_REJECT_QUEUE_FULL);
    return false;
  }

  g_setpoints_apply_in_progress = true;
  cfg_report_result_nolock(token, CFG_RESULT_QUEUED, g_active_config.version);
  s_last_submit_token = token;
  return true;
}

static bool topo_try_submit(uint16_t token)
{
  topology_chunk_req_t req = {0};
  uint16_t i;

  req.request_token = token;
  req.chunk_index = s_holding[topo_index(TOPO_OFF_REQ_CHUNK_INDEX)];
  req.chunk_words = s_holding[topo_index(TOPO_OFF_REQ_CHUNK_WORDS)];
  req.flags = s_holding[topo_index(TOPO_OFF_REQ_FLAGS)];
  req.total_size = topo_get_u32(TOPO_OFF_REQ_TOTAL_SIZE_HI, TOPO_OFF_REQ_TOTAL_SIZE_LO);
  req.chunk_crc = topo_get_u32(TOPO_OFF_REQ_CHUNK_CRC_HI, TOPO_OFF_REQ_CHUNK_CRC_LO);
  req.generation = topo_get_u32(TOPO_OFF_REQ_GEN_HI, TOPO_OFF_REQ_GEN_LO);

  if (req.chunk_words > TOPOLOGY_UPLOAD_CHUNK_WORDS)
  {
    topo_report_result_nolock(token,
                              CFG_RESULT_REJECT_TOPOLOGY_BOUNDS,
                              g_topology_v2_generation,
                              g_topology_v2_active_size);
    publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_REJECT_TOPOLOGY_BOUNDS);
    return false;
  }

  for (i = 0U; i < req.chunk_words; i++)
  {
    req.chunk_data[i] = s_holding[topo_index((uint16_t)(TOPO_OFF_CHUNK_BASE + i))];
  }

  if ((osKernelGetState() != osKernelRunning) || (qTopologyStoreHandle == NULL))
  {
    topo_report_result_nolock(token,
                              CFG_RESULT_REJECT_QUEUE_FULL,
                              g_topology_v2_generation,
                              g_topology_v2_active_size);
    publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_REJECT_QUEUE_FULL);
    return false;
  }

  if (osMessageQueuePut(qTopologyStoreHandle, &req, 0U, 0U) != osOK)
  {
    topo_report_result_nolock(token,
                              CFG_RESULT_REJECT_QUEUE_FULL,
                              g_topology_v2_generation,
                              g_topology_v2_active_size);
    publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_REJECT_QUEUE_FULL);
    return false;
  }

  topo_report_result_nolock(token, CFG_RESULT_QUEUED, g_topology_v2_generation, g_topology_v2_active_size);
  s_last_topo_submit_token = token;
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

static void topo_maybe_submit_after_write(uint16_t start_idx, uint16_t qty)
{
  uint16_t submit_idx = topo_index(TOPO_OFF_SUBMIT_TOKEN);
  uint16_t token;

  if ((start_idx > submit_idx) || ((start_idx + qty) <= submit_idx))
  {
    return;
  }

  token = s_holding[submit_idx];
  if ((token == 0U) || (token == s_last_topo_submit_token))
  {
    return;
  }

  (void)topo_try_submit(token);
}

static bool cmd_validate_write_nolock(uint16_t start_idx, uint16_t qty, const uint16_t *values)
{
  uint32_t write_start = start_idx;
  uint32_t write_end = write_start + qty;
  uint32_t cmd_start = GH_MB_CMD_BASE;
  uint32_t cmd_end = GH_MB_CMD_BASE + GH_MB_CMD_REGS;
  uint16_t i;
  uint16_t off;

  if ((qty == 0U) || (values == NULL))
  {
    return false;
  }
  if ((write_start >= cmd_end) || (write_end <= cmd_start))
  {
    return true;
  }
  if ((write_start < cmd_start) || (write_end > cmd_end))
  {
    return false;
  }

  for (i = 0U; i < qty; i++)
  {
    off = (uint16_t)((write_start + i) - cmd_start);
    if (off >= CMD_OFF_LAST_APPLIED_TRIGGER)
    {
      /* Result and applied-trace fields are runtime-owned and read-only for clients. */
      return false;
    }
    if ((off == CMD_OFF_PAYLOAD_LEN) && (values[i] > GH_MB_CMD_PAYLOAD_WORDS))
    {
      return false;
    }
  }

  return true;
}

static bool cmd_validate_request_nolock(const gh_data_driven_command_request_t *req, uint16_t *out_result)
{
  if ((req == NULL) || (out_result == NULL))
  {
    return false;
  }

  if ((req->slave_id == 0U) || (req->slave_id > GH_MB_MAX_SLAVES))
  {
    *out_result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
    return false;
  }
  if ((req->module_id == 0U) || (req->cmd_profile_id == 0U))
  {
    *out_result = GH_MB_DCMD_RESULT_REJECT_PARTIAL;
    return false;
  }
  if ((req->payload_len == 0U) || (req->payload_len > GH_MB_CMD_PAYLOAD_WORDS))
  {
    *out_result = GH_MB_DCMD_RESULT_REJECT_BOUNDS;
    return false;
  }

  *out_result = GH_MB_DCMD_RESULT_QUEUED;
  return true;
}

static bool cmd_try_submit(uint16_t trigger)
{
  gh_data_driven_command_request_t req = {0};
  uint16_t i;
  uint16_t result_code = GH_MB_DCMD_RESULT_REJECT_PARTIAL;
  uint16_t base = GH_MB_CMD_BASE;

  if (s_cmd_pending)
  {
    s_holding[base + CMD_OFF_RESULT] = GH_MB_DCMD_RESULT_REJECT_BUSY;
    s_holding[base + CMD_OFF_IO_ERR] = (uint16_t)MODBUS_IO_ERR_NONE;
    return false;
  }

  req.trigger = trigger;
  req.slave_id = (uint8_t)(s_holding[base + CMD_OFF_TARGET_SLAVE_ID] & 0x00FFU);
  req.module_id = s_holding[base + CMD_OFF_TARGET_MODULE_ID];
  req.cmd_profile_id = s_holding[base + CMD_OFF_CMD_PROFILE_ID];
  req.payload_len = s_holding[base + CMD_OFF_PAYLOAD_LEN];
  for (i = 0U; i < GH_MB_CMD_PAYLOAD_WORDS; i++)
  {
    req.payload[i] = s_holding[base + CMD_OFF_PAYLOAD_BASE + i];
  }

  if (!cmd_validate_request_nolock(&req, &result_code))
  {
    s_holding[base + CMD_OFF_RESULT] = result_code;
    s_holding[base + CMD_OFF_IO_ERR] = (uint16_t)MODBUS_IO_ERR_NONE;
    return false;
  }

  s_cmd_pending_request = req;
  s_cmd_pending = true;
  s_holding[base + CMD_OFF_RESULT] = GH_MB_DCMD_RESULT_QUEUED;
  s_holding[base + CMD_OFF_IO_ERR] = (uint16_t)MODBUS_IO_ERR_NONE;
  return true;
}

static void cmd_maybe_submit_after_write(uint16_t start_idx, uint16_t qty)
{
  uint16_t submit_idx = (uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TRIGGER);
  uint16_t token;

  if ((start_idx > submit_idx) || ((start_idx + qty) <= submit_idx))
  {
    return;
  }

  token = s_holding[submit_idx];
  if ((token == 0U) || (token == s_last_cmd_submit_trigger))
  {
    return;
  }

  s_last_cmd_submit_trigger = token;
  (void)cmd_try_submit(token);
}

static void dir_maybe_submit_rtc_set_after_write(uint16_t start_idx, uint16_t qty)
{
  uint16_t submit_idx = dir_index(DIR_OFF_RTC_SET_TOKEN);
  uint16_t token;
  uint16_t req_hour;
  uint16_t req_minute;

  if ((start_idx > submit_idx) || ((start_idx + qty) <= submit_idx))
  {
    return;
  }

  token = s_holding[submit_idx];
  if ((token == 0U) || (token == s_last_rtc_submit_token))
  {
    return;
  }

  s_last_rtc_submit_token = token;
  req_hour = s_holding[dir_index(DIR_OFF_RTC_SET_HOUR)];
  req_minute = s_holding[dir_index(DIR_OFF_RTC_SET_MINUTE)];
  if ((req_hour > 23U) || (req_minute > 59U))
  {
    s_rtc_set_pending = false;
    s_holding[dir_index(DIR_OFF_RTC_SET_APPLIED_TOKEN)] = token;
    s_holding[dir_index(DIR_OFF_RTC_SET_RESULT)] = GH_MB_RTC_SET_RESULT_REJECT_RANGE;
    return;
  }

  s_rtc_set_request.token = token;
  s_rtc_set_request.hour = (uint8_t)req_hour;
  s_rtc_set_request.minute = (uint8_t)req_minute;
  s_rtc_set_pending = true;
  s_holding[dir_index(DIR_OFF_RTC_SET_RESULT)] = GH_MB_RTC_SET_RESULT_QUEUED;
}

void GH_ModbusMap_Init(void)
{
  uint8_t s;
  uint16_t status_base;
  uint16_t i;
  uint16_t cmd_base_addr = GH_MB_CMD_BASE;

  (void)map_ensure_mutex();
  if (!map_lock())
  {
    return;
  }

  memset(s_holding, 0, sizeof(s_holding));
  memset(s_last_ok_ms, 0, sizeof(s_last_ok_ms));
  memset(s_point_module_id, 0, sizeof(s_point_module_id));
  s_last_submit_token = 0U;
  s_last_topo_submit_token = 0U;
  s_last_rtc_submit_token = 0U;
  s_last_cmd_submit_trigger = 0U;
  memset(&s_rtc_set_request, 0, sizeof(s_rtc_set_request));
  memset(&s_cmd_pending_request, 0, sizeof(s_cmd_pending_request));
  s_cmd_pending = false;
  s_rtc_set_pending = false;
  s_point_module_generation = 0U;
  s_map_data_version = 0U;

  for (i = 0U; i < GH_MB_POINT_MAX; i++)
  {
    s_point_age_sec[i] = 0xFFFFU;
  }

  for (s = 1U; s <= GH_MB_MAX_SLAVES; s++)
  {
    if (!slave_status_base(s, &status_base))
    {
      continue;
    }
    s_holding[status_base + ST_OFF_STATUS] = 0x0002U; /* online=0, stale=1 */
    s_holding[status_base + ST_OFF_LAST_OK_AGE_SEC] = 0xFFFFU;
    s_holding[status_base + ST_OFF_DATA_VERSION] = 0U;
  }

  s_holding[cmd_base_addr + CMD_OFF_TARGET_SLAVE_ID] = 0U;
  s_holding[cmd_base_addr + CMD_OFF_TARGET_MODULE_ID] = 0U;
  s_holding[cmd_base_addr + CMD_OFF_CMD_PROFILE_ID] = 0U;
  s_holding[cmd_base_addr + CMD_OFF_PAYLOAD_LEN] = 0U;
  for (i = 0U; i < GH_MB_CMD_PAYLOAD_WORDS; i++)
  {
    s_holding[cmd_base_addr + CMD_OFF_PAYLOAD_BASE + i] = 0U;
  }
  s_holding[cmd_base_addr + CMD_OFF_TRIGGER] = 0U;
  s_holding[cmd_base_addr + CMD_OFF_LAST_APPLIED_TRIGGER] = 0U;
  s_holding[cmd_base_addr + CMD_OFF_RESULT] = GH_MB_DCMD_RESULT_IDLE;
  s_holding[cmd_base_addr + CMD_OFF_IO_ERR] = (uint16_t)MODBUS_IO_ERR_NONE;

  s_holding[cfg_index(CFG_OFF_RESULT_CODE)] = (uint16_t)CFG_RESULT_IDLE;
  s_holding[cfg_index(CFG_OFF_RESULT_TOKEN)] = 0U;
  cfg_set_u32(CFG_OFF_ACTIVE_VER_HI, CFG_OFF_ACTIVE_VER_LO, g_active_config.version);
  s_holding[cfg_index(CFG_OFF_SUBMIT_TOKEN)] = 0U;

  s_holding[topo_index(TOPO_OFF_RESULT_CODE)] = (uint16_t)CFG_RESULT_IDLE;
  s_holding[topo_index(TOPO_OFF_RESULT_TOKEN)] = 0U;
  s_holding[topo_index(TOPO_OFF_SUBMIT_TOKEN)] = 0U;
  s_holding[topo_index(TOPO_OFF_REQ_CHUNK_WORDS)] = TOPOLOGY_UPLOAD_CHUNK_WORDS;
  s_holding[topo_index(TOPO_OFF_REQ_FLAGS)] = 0U;
  s_holding[dir_index(DIR_OFF_RTC_SET_TOKEN)] = 0U;
  s_holding[dir_index(DIR_OFF_RTC_SET_APPLIED_TOKEN)] = 0U;
  s_holding[dir_index(DIR_OFF_RTC_SET_RESULT)] = GH_MB_RTC_SET_RESULT_IDLE;

  map_refresh_runtime_diag_nolock();
  map_unlock();
}

void GH_ModbusMap_UpdateAges(uint32_t now_ms)
{
  uint8_t s;
  uint16_t status_base;
  uint32_t age_sec;
  uint16_t i;
  uint32_t ts_ms;

  if (!map_lock())
  {
    return;
  }

  for (s = 1U; s <= GH_MB_MAX_SLAVES; s++)
  {
    if (!slave_status_base(s, &status_base))
    {
      continue;
    }

    if (s_last_ok_ms[s - 1U] == 0U)
    {
      s_holding[status_base + ST_OFF_LAST_OK_AGE_SEC] = 0xFFFFU;
      continue;
    }

    age_sec = (now_ms - s_last_ok_ms[s - 1U]) / 1000U;
    if (age_sec > 0xFFFFU)
    {
      age_sec = 0xFFFFU;
    }
    s_holding[status_base + ST_OFF_LAST_OK_AGE_SEC] = (uint16_t)age_sec;
  }

  for (i = 0U; i < GH_MB_POINT_MAX; i++)
  {
    ts_ms = g_sensors[i].timestamp_ms;
    if (ts_ms == 0U)
    {
      s_point_age_sec[i] = 0xFFFFU;
      continue;
    }

    age_sec = (now_ms - ts_ms) / 1000U;
    if (age_sec > 0xFFFFU)
    {
      age_sec = 0xFFFFU;
    }
    s_point_age_sec[i] = (uint16_t)age_sec;
  }

  s_map_data_version++;
  map_unlock();
}

void GH_ModbusMap_UpdateRtcTime(uint8_t hour, uint8_t minute)
{
  if ((hour > 23U) || (minute > 59U))
  {
    return;
  }

  if (!map_lock())
  {
    return;
  }

  s_holding[dir_index(DIR_OFF_RTC_HOUR)] = (uint16_t)hour;
  s_holding[dir_index(DIR_OFF_RTC_MINUTE)] = (uint16_t)minute;
  map_unlock();
}

bool GH_ModbusMap_GetRtcSetRequest(gh_rtc_set_request_t *out_req)
{
  if (out_req == NULL)
  {
    return false;
  }

  if (!map_lock())
  {
    return false;
  }

  if (!s_rtc_set_pending)
  {
    map_unlock();
    return false;
  }

  *out_req = s_rtc_set_request;
  s_rtc_set_pending = false;
  map_unlock();
  return true;
}

void GH_ModbusMap_MarkRtcSetResult(uint16_t token, bool applied, uint8_t hour, uint8_t minute)
{
  if (!map_lock())
  {
    return;
  }

  s_holding[dir_index(DIR_OFF_RTC_SET_APPLIED_TOKEN)] = token;
  if (applied && (hour <= 23U) && (minute <= 59U))
  {
    s_holding[dir_index(DIR_OFF_RTC_HOUR)] = (uint16_t)hour;
    s_holding[dir_index(DIR_OFF_RTC_MINUTE)] = (uint16_t)minute;
    s_holding[dir_index(DIR_OFF_RTC_SET_RESULT)] = GH_MB_RTC_SET_RESULT_APPLIED;
  }
  else
  {
    s_holding[dir_index(DIR_OFF_RTC_SET_RESULT)] = GH_MB_RTC_SET_RESULT_FAILED;
  }

  map_unlock();
}

void GH_ModbusMap_ReportRtcSyncDiag(uint32_t attempts,
                                    uint32_t success,
                                    uint32_t failed,
                                    uint16_t last_slave_id,
                                    uint16_t last_token,
                                    uint16_t last_result)
{
  if (!map_lock())
  {
    return;
  }

  dir_set_u32(DIR_OFF_RTC_SYNC_ATTEMPT_HI, DIR_OFF_RTC_SYNC_ATTEMPT_LO, attempts);
  dir_set_u32(DIR_OFF_RTC_SYNC_OK_HI, DIR_OFF_RTC_SYNC_OK_LO, success);
  dir_set_u32(DIR_OFF_RTC_SYNC_FAIL_HI, DIR_OFF_RTC_SYNC_FAIL_LO, failed);
  s_holding[dir_index(DIR_OFF_RTC_SYNC_LAST_SLAVE)] = last_slave_id;
  s_holding[dir_index(DIR_OFF_RTC_SYNC_LAST_TOKEN)] = last_token;
  s_holding[dir_index(DIR_OFF_RTC_SYNC_LAST_RESULT)] = last_result;
  map_unlock();
}

bool GH_ModbusMap_ReadRange(uint16_t start_addr, uint16_t qty, uint16_t *out_regs)
{
  uint16_t idx;

  if ((out_regs == NULL) || !addr_to_index(start_addr, qty, &idx))
  {
    return false;
  }

  if (!map_lock())
  {
    return false;
  }
  map_refresh_runtime_diag_nolock();
  memcpy(out_regs, &s_holding[idx], (uint32_t)qty * sizeof(uint16_t));
  map_unlock();
  return true;
}

bool GH_ModbusMap_WriteSingle(uint16_t addr, uint16_t value)
{
  uint16_t idx;
  uint16_t one[1];

  if (!addr_to_index(addr, 1U, &idx))
  {
    return false;
  }
  if (!map_lock())
  {
    return false;
  }
  one[0] = value;
  if (!cmd_validate_write_nolock(idx, 1U, one))
  {
    map_unlock();
    return false;
  }
  s_holding[idx] = value;
  cfg_maybe_submit_after_write(idx, 1U);
  topo_maybe_submit_after_write(idx, 1U);
  cmd_maybe_submit_after_write(idx, 1U);
  dir_maybe_submit_rtc_set_after_write(idx, 1U);
  map_unlock();
  return true;
}

bool GH_ModbusMap_WriteRange(uint16_t start_addr, uint16_t qty, const uint16_t *values)
{
  uint16_t idx;

  if ((values == NULL) || !addr_to_index(start_addr, qty, &idx))
  {
    return false;
  }

  if (!map_lock())
  {
    return false;
  }
  if (!cmd_validate_write_nolock(idx, qty, values))
  {
    map_unlock();
    return false;
  }
  memcpy(&s_holding[idx], values, (uint32_t)qty * sizeof(uint16_t));
  cfg_maybe_submit_after_write(idx, qty);
  topo_maybe_submit_after_write(idx, qty);
  cmd_maybe_submit_after_write(idx, qty);
  dir_maybe_submit_rtc_set_after_write(idx, qty);
  map_unlock();
  return true;
}

void GH_ModbusMap_UpdateTelemetry(uint8_t slave_id,
                                  const uint16_t *sensors_9,
                                  uint16_t valid_mask,
                                  uint16_t out_state_mask,
                                  uint32_t now_ms)
{
  uint16_t status_base;

  (void)sensors_9;

  if (!slave_status_base(slave_id, &status_base))
  {
    return;
  }
  if (!map_lock())
  {
    return;
  }

  s_holding[status_base + ST_OFF_STATUS] = 0x0001U; /* online=1, stale=0 */
  s_holding[status_base + ST_OFF_LAST_OK_AGE_SEC] = 0U;
  s_holding[status_base + ST_OFF_VALID_MASK] = valid_mask;
  s_holding[status_base + ST_OFF_OUT_STATE_MASK] = out_state_mask;
  s_last_ok_ms[slave_id - 1U] = now_ms;
  bump_slave_data_version_nolock(status_base);
  map_unlock();
}

void GH_ModbusMap_ReportTimeout(uint8_t slave_id, uint32_t now_ms)
{
  uint16_t status_base;
  uint32_t last_ok_ms;
  uint32_t elapsed_ms;

  if (!slave_status_base(slave_id, &status_base))
  {
    return;
  }
  if (!map_lock())
  {
    return;
  }

  last_ok_ms = s_last_ok_ms[slave_id - 1U];
  if (last_ok_ms == 0U)
  {
    elapsed_ms = 0xFFFFFFFFUL;
  }
  else
  {
    elapsed_ms = now_ms - last_ok_ms;
  }

  s_holding[status_base + ST_OFF_ERR_TIMEOUT]++;
  if ((last_ok_ms != 0U) && (elapsed_ms < MODBUS_OFFLINE_REPROBE_MS))
  {
    s_holding[status_base + ST_OFF_STATUS] = 0x0001U; /* online=1, stale=0 */
  }
  else
  {
    s_holding[status_base + ST_OFF_STATUS] = 0x0002U; /* online=0, stale=1 */
  }
  map_unlock();
}

void GH_ModbusMap_UpdateDiag(uint8_t slave_id,
                             uint16_t err_timeout,
                             uint16_t err_crc,
                             uint16_t err_exception)
{
  uint16_t status_base;
  if (!slave_status_base(slave_id, &status_base))
  {
    return;
  }
  if (!map_lock())
  {
    return;
  }
  s_holding[status_base + ST_OFF_ERR_TIMEOUT] = err_timeout;
  s_holding[status_base + ST_OFF_ERR_CRC] = err_crc;
  s_holding[status_base + ST_OFF_ERR_EXCEPTION] = err_exception;
  map_unlock();
}

bool GH_ModbusMap_GetDataDrivenCommandRequest(gh_data_driven_command_request_t *out_req)
{
  if (out_req == NULL)
  {
    return false;
  }
  if (!map_lock())
  {
    return false;
  }
  if (!s_cmd_pending)
  {
    map_unlock();
    return false;
  }

  *out_req = s_cmd_pending_request;
  map_unlock();
  return true;
}

void GH_ModbusMap_MarkDataDrivenCommandResult(const gh_data_driven_command_result_t *result)
{
  uint16_t base = GH_MB_CMD_BASE;
  uint16_t status_base;

  if (result == NULL)
  {
    return;
  }
  if (!map_lock())
  {
    return;
  }
  if (!s_cmd_pending || (result->trigger != s_cmd_pending_request.trigger))
  {
    map_unlock();
    return;
  }

  s_cmd_pending = false;
  s_holding[base + CMD_OFF_LAST_APPLIED_TRIGGER] = result->trigger;
  s_holding[base + CMD_OFF_RESULT] = result->result;
  s_holding[base + CMD_OFF_IO_ERR] = (uint16_t)result->io_error;
  if (slave_status_base(s_cmd_pending_request.slave_id, &status_base))
  {
    bump_slave_data_version_nolock(status_base);
  }
  map_unlock();
}

uint16_t *GH_ModbusMap_GetBackingStore(void)
{
  return s_holding;
}

uint16_t GH_ModbusMap_GetBackingStoreSize(void)
{
  return GH_MB_TOTAL_REGS;
}
