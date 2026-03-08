#include "gh_crc32.h"
#include "gh_modbus_map.h"
#include "test_common.h"
#include "ut_os_hooks.h"

#include <string.h>

#define CFG_OFF_SUBMIT_TOKEN 0U
#define CFG_OFF_RESULT_CODE 1U
#define CFG_OFF_RESULT_TOKEN 2U
#define CFG_OFF_ACTIVE_VER_HI 3U
#define CFG_OFF_ACTIVE_VER_LO 4U
#define CFG_OFF_REQ_VER_HI 10U
#define CFG_OFF_REQ_VER_LO 11U
#define CFG_OFF_REQ_CRC_HI 12U
#define CFG_OFF_REQ_CRC_LO 13U
#define CFG_OFF_PAYLOAD_BASE 16U

#define TOPO_OFF_SUBMIT_TOKEN 0U
#define TOPO_OFF_RESULT_CODE 1U
#define TOPO_OFF_RESULT_TOKEN 2U
#define TOPO_OFF_ACTIVE_GEN_HI 6U
#define TOPO_OFF_REQ_CHUNK_INDEX 10U
#define TOPO_OFF_REQ_CHUNK_WORDS 11U
#define TOPO_OFF_REQ_TOTAL_SIZE_HI 12U
#define TOPO_OFF_REQ_TOTAL_SIZE_LO 13U
#define TOPO_OFF_REQ_CHUNK_CRC_HI 14U
#define TOPO_OFF_REQ_CHUNK_CRC_LO 15U
#define TOPO_OFF_REQ_FLAGS 16U
#define TOPO_OFF_REQ_GEN_HI 17U
#define TOPO_OFF_REQ_GEN_LO 18U
#define TOPO_OFF_CHUNK_BASE 20U

#define CMD_OFF_MODE 0U
#define SCHED_OFF_SCH0_EN 0U
#define SCHED_OFF_SCH0_ON 1U
#define SCHED_OFF_SCH0_OFF 2U
#define SCHED_OFF_SCH1_EN 3U
#define SCHED_OFF_SCH1_ON 4U
#define SCHED_OFF_SCH1_OFF 5U
#define SCHED_OFF_APPLY_VALUE 12U
#define SCHED_OFF_EXPECTED_VER_HI 13U
#define SCHED_OFF_EXPECTED_VER_LO 14U
#define SCHED_OFF_CMD_KIND 15U
#define SCHED_OFF_APPLY_TRIGGER 16U
#define SCHED_OFF_LAST_APPLIED_TRIGGER 17U
#define SCHED_OFF_LAST_RESULT 18U
#define SCHED_OFF_LAST_IO_ERR 19U
#define DIR_OFF_RTC_HOUR 14U
#define DIR_OFF_RTC_MINUTE 15U
#define DIR_OFF_RTC_SET_HOUR 16U
#define DIR_OFF_RTC_SET_MINUTE 17U
#define DIR_OFF_RTC_SET_TOKEN 18U
#define DIR_OFF_RTC_SET_APPLIED_TOKEN 19U
#define DIR_OFF_RTC_SET_RESULT 20U
#define DIR_OFF_RTC_SYNC_ATTEMPT_HI 21U
#define DIR_OFF_RTC_SYNC_ATTEMPT_LO 22U
#define DIR_OFF_RTC_SYNC_OK_HI 23U
#define DIR_OFF_RTC_SYNC_OK_LO 24U
#define DIR_OFF_RTC_SYNC_FAIL_HI 25U
#define DIR_OFF_RTC_SYNC_FAIL_LO 26U
#define DIR_OFF_RTC_SYNC_LAST_SLAVE 27U
#define DIR_OFF_RTC_SYNC_LAST_TOKEN 28U
#define DIR_OFF_RTC_SYNC_LAST_RESULT 29U

int test_modbus_map_run(void)
{
  uint16_t regs[4] = {0U};
  uint16_t regs_u32[6] = {0U};
  uint16_t payload_words[CONFIG_PAYLOAD_SIZE / 2U] = {0U};
  uint8_t payload[CONFIG_PAYLOAD_SIZE] = {0U};
  uint32_t crc;
  uint32_t req_version = 42U;
  uint16_t req_hi = (uint16_t)((req_version >> 16U) & 0xFFFFU);
  uint16_t req_lo = (uint16_t)(req_version & 0xFFFFU);
  uint16_t crc_hi;
  uint16_t crc_lo;
  uint16_t token = 7U;
  uint16_t topo_words[4] = {0x1122U, 0x3344U, 0x5566U, 0x7788U};
  uint8_t topo_bytes[8] = {0U};
  uint32_t topo_crc;
  uint32_t topo_total_size = 512U;
  uint32_t topo_generation = 9U;
  uint16_t topo_crc_hi;
  uint16_t topo_crc_lo;
  uint16_t topo_gen_hi;
  uint16_t topo_gen_lo;
  uint16_t topo_size_hi;
  uint16_t topo_size_lo;
  uint16_t topo_token = 22U;
  uint32_t expected_ver = 0x12345678UL;
  uint16_t expected_hi = (uint16_t)((expected_ver >> 16U) & 0xFFFFU);
  uint16_t expected_lo = (uint16_t)(expected_ver & 0xFFFFU);
  schedule_apply_request_t sched_req = {0};
  schedule_apply_result_t sched_result = {0};
  gh_rtc_set_request_t rtc_req = {0};

  UT_OsHooks_Reset();
  memset(&g_status, 0, sizeof(g_status));
  g_active_config.version = 11U;
  qConfigStoreHandle = (osMessageQueueId_t)0x11;
  qTopologyStoreHandle = (osMessageQueueId_t)0x22;
  g_setpoints_apply_in_progress = false;

  GH_ModbusMap_Init();

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + 0U), 1U, regs));
  UT_ASSERT_EQ_U32(3U, regs[0]);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + GH_MB_DIR_OFF_SCHED_BASE), 2U, regs));
  UT_ASSERT_EQ_U32(GH_MB_SCHED_BASE, regs[0]);
  UT_ASSERT_EQ_U32(GH_MB_SCHED_BLOCK_SIZE, regs[1]);

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_RESULT_CODE), 1U, regs));
  UT_ASSERT_EQ_U32(CFG_RESULT_IDLE, regs[0]);

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_ACTIVE_VER_HI), 2U, regs));
  UT_ASSERT_EQ_U32((g_active_config.version >> 16U) & 0xFFFFU, regs[0]);
  UT_ASSERT_EQ_U32(g_active_config.version & 0xFFFFU, regs[1]);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_MODE), 1234U));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_MODE), 1U, regs));
  UT_ASSERT_EQ_U32(1234U, regs[0]);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_SCH0_EN), 1U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_SCH0_ON), 2300U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_SCH0_OFF), 100U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_SCH1_EN), 0U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_SCH1_ON), 830U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_SCH1_OFF), 1730U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_APPLY_VALUE), 1U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_EXPECTED_VER_HI), expected_hi));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_EXPECTED_VER_LO), expected_lo));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_CMD_KIND),
                                          GH_MB_SCHED_CMD_KIND_REMOTE_SCHEDULE));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_APPLY_TRIGGER), 77U));

  UT_ASSERT_TRUE(GH_ModbusMap_GetScheduleApplyRequest(1U, &sched_req));
  UT_ASSERT_EQ_U32(77U, sched_req.trigger);
  UT_ASSERT_EQ_U32(1U, sched_req.apply_value);
  UT_ASSERT_EQ_U32(expected_ver, sched_req.expected_active_ctrl_version);
  UT_ASSERT_EQ_U32(GH_MB_SCHED_CMD_KIND_REMOTE_SCHEDULE, sched_req.cmd_kind);
  UT_ASSERT_EQ_U32(1U, sched_req.slots[0].enabled);
  UT_ASSERT_EQ_U32(2300U, sched_req.slots[0].on_hhmm);
  UT_ASSERT_EQ_U32(100U, sched_req.slots[0].off_hhmm);
  UT_ASSERT_EQ_U32(0U, sched_req.slots[1].enabled);

  sched_result.trigger = sched_req.trigger;
  sched_result.result = GH_MB_SCHED_RESULT_APPLIED;
  sched_result.io_error = MODBUS_IO_ERR_NONE;
  GH_ModbusMap_MarkScheduleApplyResult(1U, &sched_result);
  UT_ASSERT_TRUE(!GH_ModbusMap_GetScheduleApplyRequest(1U, &sched_req));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_LAST_APPLIED_TRIGGER), 3U, regs));
  UT_ASSERT_EQ_U32(77U, regs[0]);
  UT_ASSERT_EQ_U32(GH_MB_SCHED_RESULT_APPLIED, regs[1]);
  UT_ASSERT_EQ_U32(MODBUS_IO_ERR_NONE, regs[2]);
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_APPLY_TRIGGER), 78U));
  UT_ASSERT_TRUE(GH_ModbusMap_GetScheduleApplyRequest(1U, &sched_req));
  UT_ASSERT_EQ_U32(78U, sched_req.trigger);
  sched_result.trigger = sched_req.trigger;
  sched_result.result = EVENT_CODE_CTRL_SYNC_TRANSPORT_TIMEOUT;
  sched_result.io_error = MODBUS_IO_ERR_TIMEOUT;
  GH_ModbusMap_MarkScheduleApplyResult(1U, &sched_result);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_SCHED_BASE + SCHED_OFF_LAST_APPLIED_TRIGGER), 3U, regs));
  UT_ASSERT_EQ_U32(77U, regs[0]);
  UT_ASSERT_EQ_U32(EVENT_CODE_CTRL_SYNC_TRANSPORT_TIMEOUT, regs[1]);
  UT_ASSERT_EQ_U32(MODBUS_IO_ERR_TIMEOUT, regs[2]);
  UT_ASSERT_TRUE(GH_ModbusMap_GetScheduleApplyRequest(1U, &sched_req));
  UT_ASSERT_EQ_U32(78U, sched_req.trigger);
  sched_result.trigger = sched_req.trigger;
  sched_result.result = GH_MB_SCHED_RESULT_APPLIED;
  sched_result.io_error = MODBUS_IO_ERR_NONE;
  GH_ModbusMap_MarkScheduleApplyResult(1U, &sched_result);
  UT_ASSERT_TRUE(!GH_ModbusMap_GetScheduleApplyRequest(1U, &sched_req));

  GH_ModbusMap_UpdateRtcTime(9U, 37U);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_HOUR), 2U, regs));
  UT_ASSERT_EQ_U32(9U, regs[0]);
  UT_ASSERT_EQ_U32(37U, regs[1]);
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_HOUR), 13U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_MINUTE), 50U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_TOKEN), 31U));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_RESULT), 1U, regs));
  UT_ASSERT_EQ_U32(GH_MB_RTC_SET_RESULT_QUEUED, regs[0]);
  UT_ASSERT_TRUE(GH_ModbusMap_GetRtcSetRequest(&rtc_req));
  UT_ASSERT_EQ_U32(31U, rtc_req.token);
  UT_ASSERT_EQ_U32(13U, rtc_req.hour);
  UT_ASSERT_EQ_U32(50U, rtc_req.minute);
  GH_ModbusMap_MarkRtcSetResult(rtc_req.token, true, rtc_req.hour, rtc_req.minute);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_APPLIED_TOKEN), 2U, regs));
  UT_ASSERT_EQ_U32(31U, regs[0]);
  UT_ASSERT_EQ_U32(GH_MB_RTC_SET_RESULT_APPLIED, regs[1]);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_HOUR), 2U, regs));
  UT_ASSERT_EQ_U32(13U, regs[0]);
  UT_ASSERT_EQ_U32(50U, regs[1]);
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_HOUR), 25U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_MINUTE), 10U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_TOKEN), 32U));
  UT_ASSERT_TRUE(!GH_ModbusMap_GetRtcSetRequest(&rtc_req));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SET_APPLIED_TOKEN), 2U, regs));
  UT_ASSERT_EQ_U32(32U, regs[0]);
  UT_ASSERT_EQ_U32(GH_MB_RTC_SET_RESULT_REJECT_RANGE, regs[1]);
  GH_ModbusMap_ReportRtcSyncDiag(10U, 8U, 2U, 20U, 100U, 5U);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SYNC_ATTEMPT_HI), 6U, regs_u32));
  UT_ASSERT_EQ_U32(10U, (((uint32_t)regs_u32[0] << 16U) | regs_u32[1]));
  UT_ASSERT_EQ_U32(8U, (((uint32_t)regs_u32[2] << 16U) | regs_u32[3]));
  UT_ASSERT_EQ_U32(2U, (((uint32_t)regs_u32[4] << 16U) | regs_u32[5]));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + DIR_OFF_RTC_SYNC_LAST_SLAVE), 3U, regs));
  UT_ASSERT_EQ_U32(20U, regs[0]);
  UT_ASSERT_EQ_U32(100U, regs[1]);
  UT_ASSERT_EQ_U32(5U, regs[2]);

  crc = gh_crc32_compute(payload, CONFIG_PAYLOAD_SIZE);
  crc_hi = (uint16_t)((crc >> 16U) & 0xFFFFU);
  crc_lo = (uint16_t)(crc & 0xFFFFU);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_REQ_VER_HI), req_hi));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_REQ_VER_LO), req_lo));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_REQ_CRC_HI), crc_hi));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_REQ_CRC_LO), crc_lo));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_PAYLOAD_BASE),
                                         (uint16_t)(CONFIG_PAYLOAD_SIZE / 2U),
                                         payload_words));

  UT_ASSERT_EQ_U32(0U, ut_queue_put_count);
  UT_ASSERT_EQ_U32(0U, ut_queue_put_count_config);
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_SUBMIT_TOKEN), token));
  UT_ASSERT_EQ_U32(1U, ut_queue_put_count);
  UT_ASSERT_EQ_U32(1U, ut_queue_put_count_config);
  UT_ASSERT_EQ_U32(token, ut_last_queue_req.request_token);
  UT_ASSERT_EQ_U32(req_version, ut_last_queue_req.version);
  UT_ASSERT_EQ_U32(crc, ut_last_queue_req.payload_crc);
  UT_ASSERT_TRUE(g_setpoints_apply_in_progress);

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_RESULT_CODE), 1U, regs));
  UT_ASSERT_EQ_U32(CFG_RESULT_QUEUED, regs[0]);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_RESULT_TOKEN), 1U, regs));
  UT_ASSERT_EQ_U32(token, regs[0]);

  topo_bytes[0] = 0x11U;
  topo_bytes[1] = 0x22U;
  topo_bytes[2] = 0x33U;
  topo_bytes[3] = 0x44U;
  topo_bytes[4] = 0x55U;
  topo_bytes[5] = 0x66U;
  topo_bytes[6] = 0x77U;
  topo_bytes[7] = 0x88U;
  topo_crc = gh_crc32_compute(topo_bytes, sizeof(topo_bytes));
  topo_crc_hi = (uint16_t)((topo_crc >> 16U) & 0xFFFFU);
  topo_crc_lo = (uint16_t)(topo_crc & 0xFFFFU);
  topo_gen_hi = (uint16_t)((topo_generation >> 16U) & 0xFFFFU);
  topo_gen_lo = (uint16_t)(topo_generation & 0xFFFFU);
  topo_size_hi = (uint16_t)((topo_total_size >> 16U) & 0xFFFFU);
  topo_size_lo = (uint16_t)(topo_total_size & 0xFFFFU);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_CHUNK_INDEX), 0U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_CHUNK_WORDS), 4U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_TOTAL_SIZE_HI), topo_size_hi));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_TOTAL_SIZE_LO), topo_size_lo));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_CHUNK_CRC_HI), topo_crc_hi));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_CHUNK_CRC_LO), topo_crc_lo));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_FLAGS), 0U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_GEN_HI), topo_gen_hi));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_GEN_LO), topo_gen_lo));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteRange((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_CHUNK_BASE), 4U, topo_words));

  UT_ASSERT_EQ_U32(0U, ut_queue_put_count_topology);
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_SUBMIT_TOKEN), topo_token));
  UT_ASSERT_EQ_U32(1U, ut_queue_put_count_topology);
  UT_ASSERT_EQ_U32(topo_token, ut_last_topology_queue_req.request_token);
  UT_ASSERT_EQ_U32(0U, ut_last_topology_queue_req.chunk_index);
  UT_ASSERT_EQ_U32(4U, ut_last_topology_queue_req.chunk_words);
  UT_ASSERT_EQ_U32(topo_total_size, ut_last_topology_queue_req.total_size);
  UT_ASSERT_EQ_U32(topo_crc, ut_last_topology_queue_req.chunk_crc);
  UT_ASSERT_EQ_U32(topo_generation, ut_last_topology_queue_req.generation);
  UT_ASSERT_EQ_U32(topo_words[0], ut_last_topology_queue_req.chunk_data[0]);
  UT_ASSERT_EQ_U32(topo_words[3], ut_last_topology_queue_req.chunk_data[3]);

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_RESULT_CODE), 1U, regs));
  UT_ASSERT_EQ_U32(CFG_RESULT_QUEUED, regs[0]);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_RESULT_TOKEN), 1U, regs));
  UT_ASSERT_EQ_U32(topo_token, regs[0]);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_REQ_CHUNK_WORDS),
                                          (uint16_t)(TOPOLOGY_UPLOAD_CHUNK_WORDS + 1U)));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_SUBMIT_TOKEN), (uint16_t)(topo_token + 1U)));
  UT_ASSERT_EQ_U32(1U, ut_queue_put_count_topology);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_RESULT_CODE), 1U, regs));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_BOUNDS, regs[0]);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_RESULT_TOKEN), 1U, regs));
  UT_ASSERT_EQ_U32((uint16_t)(topo_token + 1U), regs[0]);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_TOPO_BASE + TOPO_OFF_ACTIVE_GEN_HI), 2U, regs_u32));
  UT_ASSERT_EQ_U32(g_topology_v2_generation, (((uint32_t)regs_u32[0] << 16U) | regs_u32[1]));

  return 0;
}
