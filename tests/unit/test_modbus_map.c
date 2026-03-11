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

#define CMD_OFF_TARGET_SLAVE_ID 0U
#define CMD_OFF_TARGET_MODULE_ID 1U
#define CMD_OFF_CMD_PROFILE_ID 2U
#define CMD_OFF_PAYLOAD_LEN 3U
#define CMD_OFF_PAYLOAD_BASE 4U
#define CMD_OFF_TRIGGER (CMD_OFF_PAYLOAD_BASE + GH_MB_CMD_PAYLOAD_WORDS)
#define CMD_OFF_LAST_APPLIED_TRIGGER (CMD_OFF_TRIGGER + 1U)
#define CMD_OFF_RESULT (CMD_OFF_TRIGGER + 2U)
#define CMD_OFF_IO_ERR (CMD_OFF_TRIGGER + 3U)
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
  gh_data_driven_command_request_t dcmd_req = {0};
  gh_data_driven_command_result_t dcmd_result = {0};
  gh_rtc_set_request_t rtc_req = {0};

  UT_OsHooks_Reset();
  memset(&g_status, 0, sizeof(g_status));
  g_active_config.version = 11U;
  qConfigStoreHandle = (osMessageQueueId_t)0x11;
  qTopologyStoreHandle = (osMessageQueueId_t)0x22;
  g_setpoints_apply_in_progress = false;

  GH_ModbusMap_Init();

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_DIR_BASE + 0U), 1U, regs));
  UT_ASSERT_EQ_U32(4U, regs[0]);

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_RESULT_CODE), 1U, regs));
  UT_ASSERT_EQ_U32(CFG_RESULT_IDLE, regs[0]);

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_ACTIVE_VER_HI), 2U, regs));
  UT_ASSERT_EQ_U32((g_active_config.version >> 16U) & 0xFFFFU, regs[0]);
  UT_ASSERT_EQ_U32(g_active_config.version & 0xFFFFU, regs[1]);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TARGET_SLAVE_ID), 1U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TARGET_MODULE_ID), 101U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_CMD_PROFILE_ID), 5001U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_LEN), 3U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_BASE + 0U), 11U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_BASE + 1U), 22U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_BASE + 2U), 33U));
  UT_ASSERT_TRUE(!GH_ModbusMap_GetDataDrivenCommandRequest(&dcmd_req));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TRIGGER), 77U));
  UT_ASSERT_TRUE(GH_ModbusMap_GetDataDrivenCommandRequest(&dcmd_req));
  UT_ASSERT_EQ_U32(77U, dcmd_req.trigger);
  UT_ASSERT_EQ_U32(1U, dcmd_req.slave_id);
  UT_ASSERT_EQ_U32(101U, dcmd_req.module_id);
  UT_ASSERT_EQ_U32(5001U, dcmd_req.cmd_profile_id);
  UT_ASSERT_EQ_U32(3U, dcmd_req.payload_len);
  UT_ASSERT_EQ_U32(11U, dcmd_req.payload[0]);
  UT_ASSERT_EQ_U32(33U, dcmd_req.payload[2]);

  dcmd_result.trigger = dcmd_req.trigger;
  dcmd_result.result = GH_MB_DCMD_RESULT_APPLIED;
  dcmd_result.io_error = MODBUS_IO_ERR_NONE;
  GH_ModbusMap_MarkDataDrivenCommandResult(&dcmd_result);
  UT_ASSERT_TRUE(!GH_ModbusMap_GetDataDrivenCommandRequest(&dcmd_req));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_LAST_APPLIED_TRIGGER), 3U, regs));
  UT_ASSERT_EQ_U32(77U, regs[0]);
  UT_ASSERT_EQ_U32(GH_MB_DCMD_RESULT_APPLIED, regs[1]);
  UT_ASSERT_EQ_U32(MODBUS_IO_ERR_NONE, regs[2]);

  UT_ASSERT_TRUE(!GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_RESULT), 123U));
  UT_ASSERT_TRUE(!GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_LEN),
                                           (uint16_t)(GH_MB_CMD_PAYLOAD_WORDS + 1U)));

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_LEN), 1U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_BASE + 0U), 99U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TRIGGER), 78U));
  UT_ASSERT_TRUE(GH_ModbusMap_GetDataDrivenCommandRequest(&dcmd_req));
  UT_ASSERT_EQ_U32(78U, dcmd_req.trigger);
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TRIGGER), 79U));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_RESULT), 1U, regs));
  UT_ASSERT_EQ_U32(GH_MB_DCMD_RESULT_REJECT_BUSY, regs[0]);
  dcmd_result.trigger = dcmd_req.trigger;
  dcmd_result.result = GH_MB_DCMD_RESULT_APPLIED;
  dcmd_result.io_error = MODBUS_IO_ERR_NONE;
  GH_ModbusMap_MarkDataDrivenCommandResult(&dcmd_result);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_BASE + 0U), 1234U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TRIGGER), 79U));
  UT_ASSERT_TRUE(!GH_ModbusMap_GetDataDrivenCommandRequest(&dcmd_req));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_RESULT), 1U, regs));
  UT_ASSERT_EQ_U32(GH_MB_DCMD_RESULT_APPLIED, regs[0]);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TARGET_SLAVE_ID), 1U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TARGET_MODULE_ID), 0U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_CMD_PROFILE_ID), 5001U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_LEN), 1U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_BASE + 0U), 44U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TRIGGER), 90U));
  UT_ASSERT_TRUE(!GH_ModbusMap_GetDataDrivenCommandRequest(&dcmd_req));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_RESULT), 1U, regs));
  UT_ASSERT_EQ_U32(GH_MB_DCMD_RESULT_REJECT_PARTIAL, regs[0]);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TARGET_SLAVE_ID), 0U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TARGET_MODULE_ID), 101U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_CMD_PROFILE_ID), 5001U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_LEN), 1U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_BASE + 0U), 55U));
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_TRIGGER), 91U));
  UT_ASSERT_TRUE(!GH_ModbusMap_GetDataDrivenCommandRequest(&dcmd_req));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_RESULT), 1U, regs));
  UT_ASSERT_EQ_U32(GH_MB_DCMD_RESULT_REJECT_BOUNDS, regs[0]);

  {
    uint16_t readonly_overwrite[3] = {1U, 2U, 3U};
    UT_ASSERT_TRUE(!GH_ModbusMap_WriteRange((uint16_t)(GH_MB_CMD_BASE + CMD_OFF_LAST_APPLIED_TRIGGER),
                                            3U,
                                            readonly_overwrite));
  }

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
