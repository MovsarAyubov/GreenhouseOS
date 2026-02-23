#include "gh_config_storage.h"
#include "gh_crc32.h"
#include "test_common.h"

#include <string.h>

int test_config_storage_run(void)
{
  config_update_req_t req = {0};
  config_result_code_t result = CFG_RESULT_IDLE;
  float bad_value = 2000.0f;

  g_active_config.version = 10U;
  memset(req.payload, 0, sizeof(req.payload));
  req.version = 11U;
  req.payload_crc = gh_crc32_compute(req.payload, CONFIG_PAYLOAD_SIZE);

  UT_ASSERT_TRUE(GH_ConfigStorage_ValidateRequest(&req, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_IDLE, result);

  req.version = 10U;
  UT_ASSERT_TRUE(!GH_ConfigStorage_ValidateRequest(&req, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_BAD_VERSION, result);

  req.version = 11U;
  req.payload_crc ^= 0x1234U;
  UT_ASSERT_TRUE(!GH_ConfigStorage_ValidateRequest(&req, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_BAD_CRC, result);

  memcpy(&req.payload[0], &bad_value, sizeof(float));
  req.payload_crc = gh_crc32_compute(req.payload, CONFIG_PAYLOAD_SIZE);
  UT_ASSERT_TRUE(!GH_ConfigStorage_ValidateRequest(&req, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_RANGE, result);

  return 0;
}
