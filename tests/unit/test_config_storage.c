#include "gh_config_storage.h"
#include "gh_crc32.h"
#include "gh_topology_v2.h"
#include "test_common.h"

#include <string.h>

static void topo_build_min_valid_payload(uint8_t *payload)
{
  gh_topology_v2_header_t hdr = {0};

  memset(payload, 0, CONFIG_PAYLOAD_SIZE);

  hdr.magic = GH_TOPOLOGY_V2_MAGIC;
  hdr.ver_major = GH_TOPOLOGY_V2_VERSION_MAJOR;
  hdr.ver_minor = 0U;
  hdr.total_size = sizeof(hdr);
  hdr.generation = 5U;
  hdr.topology_id = 1U;
  hdr.created_unix_s = 1700000000U;
  hdr.flags = 0U;
  hdr.module_count = 0U;
  hdr.req_count = 0U;
  hdr.point_count = 0U;
  hdr.cmd_count = 0U;
  hdr.policy_count = 0U;
  hdr.off_modules = 0U;
  hdr.off_requests = 0U;
  hdr.off_points = 0U;
  hdr.off_commands = 0U;
  hdr.off_policies = 0U;
  hdr.body_crc32 = 0U;
  hdr.header_crc32 = 0U;
  hdr.header_crc32 = gh_crc32_compute((const uint8_t *)&hdr, sizeof(hdr));

  memcpy(payload, &hdr, sizeof(hdr));
}

int test_config_storage_run(void)
{
  config_update_req_t req = {0};
  config_result_code_t result = CFG_RESULT_IDLE;
  active_config_t cfg = {0};
  float bad_value = 2000.0f;
  gh_topology_v2_header_t hdr = {0};

  g_active_config.version = 10U;
  g_topology_v2_active = 0U;
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

  topo_build_min_valid_payload(req.payload);
  req.version = 12U;
  req.payload_crc = gh_crc32_compute(req.payload, CONFIG_PAYLOAD_SIZE);
  UT_ASSERT_TRUE(!GH_ConfigStorage_ValidateRequest(&req, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_SCHEMA, result);

  UT_ASSERT_TRUE(GH_TopologyV2_ValidatePayload(req.payload, CONFIG_PAYLOAD_SIZE, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_IDLE, result);

  memset(&cfg, 0, sizeof(cfg));
  memcpy(cfg.payload, req.payload, CONFIG_PAYLOAD_SIZE);
  GH_TopologyV2_SyncRuntimeFromPayload(cfg.payload, CONFIG_PAYLOAD_SIZE);
  UT_ASSERT_EQ_U32(1U, g_topology_v2_active);
  UT_ASSERT_EQ_U32(5U, g_topology_v2_generation);
  UT_ASSERT_EQ_U32(0U, g_topology_v2_module_count);

  memcpy(&hdr, req.payload, sizeof(hdr));
  hdr.header_crc32 ^= 0x1U;
  memcpy(req.payload, &hdr, sizeof(hdr));
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(req.payload, CONFIG_PAYLOAD_SIZE, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_CRC, result);

  topo_build_min_valid_payload(req.payload);
  memcpy(&hdr, req.payload, sizeof(hdr));
  hdr.module_count = (uint16_t)(GH_TOPOLOGY_V2_MAX_MODULES + 1U);
  hdr.header_crc32 = 0U;
  hdr.header_crc32 = gh_crc32_compute((const uint8_t *)&hdr, sizeof(hdr));
  memcpy(req.payload, &hdr, sizeof(hdr));
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(req.payload, CONFIG_PAYLOAD_SIZE, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_BUDGET, result);

  return 0;
}
