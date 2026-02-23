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

int test_modbus_map_run(void)
{
  uint16_t regs[4] = {0U};
  uint16_t payload_words[CONFIG_PAYLOAD_SIZE / 2U] = {0U};
  uint8_t payload[CONFIG_PAYLOAD_SIZE] = {0U};
  uint32_t crc;
  uint32_t req_version = 42U;
  uint16_t req_hi = (uint16_t)((req_version >> 16U) & 0xFFFFU);
  uint16_t req_lo = (uint16_t)(req_version & 0xFFFFU);
  uint16_t crc_hi;
  uint16_t crc_lo;
  uint16_t token = 7U;

  UT_OsHooks_Reset();
  memset(&g_status, 0, sizeof(g_status));
  g_active_config.version = 11U;
  qConfigStoreHandle = (osMessageQueueId_t)0x11;
  g_setpoints_apply_in_progress = false;

  GH_ModbusMap_Init();

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_RESULT_CODE), 1U, regs));
  UT_ASSERT_EQ_U32(CFG_RESULT_IDLE, regs[0]);

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_ACTIVE_VER_HI), 2U, regs));
  UT_ASSERT_EQ_U32((g_active_config.version >> 16U) & 0xFFFFU, regs[0]);
  UT_ASSERT_EQ_U32(g_active_config.version & 0xFFFFU, regs[1]);

  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle(5U, 1234U));
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange(5U, 1U, regs));
  UT_ASSERT_EQ_U32(1234U, regs[0]);

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
  UT_ASSERT_TRUE(GH_ModbusMap_WriteSingle((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_SUBMIT_TOKEN), token));
  UT_ASSERT_EQ_U32(1U, ut_queue_put_count);
  UT_ASSERT_EQ_U32(token, ut_last_queue_req.request_token);
  UT_ASSERT_EQ_U32(req_version, ut_last_queue_req.version);
  UT_ASSERT_EQ_U32(crc, ut_last_queue_req.payload_crc);
  UT_ASSERT_TRUE(g_setpoints_apply_in_progress);

  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_RESULT_CODE), 1U, regs));
  UT_ASSERT_EQ_U32(CFG_RESULT_QUEUED, regs[0]);
  UT_ASSERT_TRUE(GH_ModbusMap_ReadRange((uint16_t)(GH_MB_CFG_BASE + CFG_OFF_RESULT_TOKEN), 1U, regs));
  UT_ASSERT_EQ_U32(token, regs[0]);

  return 0;
}
