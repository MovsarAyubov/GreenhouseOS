#include "gh_config_storage.h"
#include "gh_crc32.h"
#include "gh_modbus_map.h"
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

static void topo_finalize_crc(uint8_t *payload, uint32_t total_size)
{
  gh_topology_v2_header_t hdr = {0};

  memcpy(&hdr, payload, sizeof(hdr));
  hdr.total_size = total_size;
  hdr.body_crc32 = gh_crc32_compute(&payload[sizeof(hdr)], total_size - sizeof(hdr));
  hdr.header_crc32 = 0U;
  hdr.header_crc32 = gh_crc32_compute((const uint8_t *)&hdr, sizeof(hdr));
  memcpy(payload, &hdr, sizeof(hdr));
}

static bool topo_build_runtime_valid_payload(uint8_t *payload, uint32_t payload_capacity, uint32_t *out_size)
{
  gh_topology_v2_header_t hdr = {0};
  gh_topology_v2_module_t mod = {0};
  gh_topology_v2_req_t req = {0};
  gh_topology_v2_point_t point = {0};
  uint32_t total_size;

  if ((payload == NULL) || (out_size == NULL))
  {
    return false;
  }

  total_size = sizeof(hdr) + sizeof(mod) + sizeof(req) + sizeof(point);
  if (payload_capacity < total_size)
  {
    return false;
  }

  memset(payload, 0, payload_capacity);

  mod.module_id = 101U;
  mod.module_type = 1U;
  mod.bus_type = 1U;
  mod.bus_index = 0U;
  mod.slave_id = 1U;
  mod.zone_id = 1U;
  mod.req_first = 0U;
  mod.req_count = 1U;
  mod.cmd_first = 0U;
  mod.cmd_count = 0U;
  mod.offline_reprobe_ms = 30000U;
  mod.heartbeat_timeout_ms = 2000U;

  req.req_id = 11U;
  req.module_id = mod.module_id;
  req.fc = 3U;
  req.priority = 0U;
  req.start_reg = 0U;
  req.reg_count = 9U;
  req.period_ms = 5000U;
  req.timeout_ms = 300U;
  req.retries = 2U;
  req.backoff_ms = 20U;
  req.point_first = 0U;
  req.point_count = 1U;
  req.flags = 0U;

  point.point_id = 1001U;
  point.module_id = mod.module_id;
  point.req_id = req.req_id;
  point.reg_offset = 0U;
  point.point_type = 1U;
  point.scale_pow10 = -1;
  point.bit_index = 0U;
  point.quality_policy = 0U;
  point.publish_index = 1U;
  point.stale_timeout_s = 30U;
  point.alarm_low = 0;
  point.alarm_high = 1000;

  hdr.magic = GH_TOPOLOGY_V2_MAGIC;
  hdr.ver_major = GH_TOPOLOGY_V2_VERSION_MAJOR;
  hdr.ver_minor = 0U;
  hdr.total_size = total_size;
  hdr.generation = 7U;
  hdr.topology_id = 123U;
  hdr.created_unix_s = 1700000000U;
  hdr.flags = 0U;
  hdr.module_count = 1U;
  hdr.req_count = 1U;
  hdr.point_count = 1U;
  hdr.cmd_count = 0U;
  hdr.policy_count = 0U;
  hdr.off_modules = sizeof(hdr);
  hdr.off_requests = hdr.off_modules + sizeof(mod);
  hdr.off_points = hdr.off_requests + sizeof(req);
  hdr.off_commands = 0U;
  hdr.off_policies = 0U;
  hdr.body_crc32 = 0U;
  hdr.header_crc32 = 0U;

  memcpy(payload, &hdr, sizeof(hdr));
  memcpy(&payload[hdr.off_modules], &mod, sizeof(mod));
  memcpy(&payload[hdr.off_requests], &req, sizeof(req));
  memcpy(&payload[hdr.off_points], &point, sizeof(point));
  topo_finalize_crc(payload, total_size);
  *out_size = total_size;
  return true;
}

static bool topo_build_runtime_valid_payload_with_command(uint8_t *payload,
                                                          uint32_t payload_capacity,
                                                          uint32_t *out_size)
{
  gh_topology_v2_header_t hdr = {0};
  gh_topology_v2_module_t mod = {0};
  gh_topology_v2_cmd_t cmd = {0};
  uint32_t total_size;

  if (!topo_build_runtime_valid_payload(payload, payload_capacity, out_size))
  {
    return false;
  }

  memcpy(&hdr, payload, sizeof(hdr));
  if (hdr.module_count == 0U)
  {
    return false;
  }

  total_size = hdr.total_size + sizeof(cmd);
  if (total_size > payload_capacity)
  {
    return false;
  }

  memcpy(&mod, &payload[hdr.off_modules], sizeof(mod));
  mod.cmd_first = 0U;
  mod.cmd_count = 1U;
  memcpy(&payload[hdr.off_modules], &mod, sizeof(mod));

  cmd.cmd_id = 2001U;
  cmd.module_id = mod.module_id;
  cmd.fc = 16U;
  cmd.retries = 2U;
  cmd.start_reg = 100U;
  cmd.max_reg_count = 6U;
  cmd.timeout_ms = 300U;
  cmd.ack_point_id = 1001U;
  cmd.flags = 0U;

  hdr.cmd_count = 1U;
  hdr.off_commands = hdr.total_size;
  memcpy(&payload[hdr.off_commands], &cmd, sizeof(cmd));
  memcpy(payload, &hdr, sizeof(hdr));
  topo_finalize_crc(payload, total_size);
  *out_size = total_size;
  return true;
}

int test_config_storage_run(void)
{
  config_update_req_t req = {0};
  config_result_code_t result = CFG_RESULT_IDLE;
  active_config_t cfg = {0};
  uint8_t topo_blob[256] = {0};
  uint32_t topo_blob_size = 0U;
  float bad_value = 2000.0f;
  gh_topology_v2_header_t hdr = {0};
  gh_topology_v2_module_t mod_row = {0};
  gh_topology_v2_req_t req_row = {0};
  gh_topology_v2_point_t point_row = {0};

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

  UT_ASSERT_TRUE(topo_build_runtime_valid_payload(topo_blob, sizeof(topo_blob), &topo_blob_size));
  UT_ASSERT_TRUE(GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_IDLE, result);

  memcpy(&hdr, topo_blob, sizeof(hdr));
  memcpy(&req_row, &topo_blob[hdr.off_requests], sizeof(req_row));
  req_row.module_id = 999U;
  memcpy(&topo_blob[hdr.off_requests], &req_row, sizeof(req_row));
  topo_finalize_crc(topo_blob, topo_blob_size);
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_SCHEMA, result);

  UT_ASSERT_TRUE(topo_build_runtime_valid_payload(topo_blob, sizeof(topo_blob), &topo_blob_size));
  memcpy(&hdr, topo_blob, sizeof(hdr));
  memcpy(&mod_row, &topo_blob[hdr.off_modules], sizeof(mod_row));
  mod_row.bus_type = 2U;
  memcpy(&topo_blob[hdr.off_modules], &mod_row, sizeof(mod_row));
  topo_finalize_crc(topo_blob, topo_blob_size);
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_SCHEMA, result);

  UT_ASSERT_TRUE(topo_build_runtime_valid_payload(topo_blob, sizeof(topo_blob), &topo_blob_size));
  memcpy(&hdr, topo_blob, sizeof(hdr));
  memcpy(&point_row, &topo_blob[hdr.off_points], sizeof(point_row));
  point_row.publish_index = SENSOR_COUNT;
  memcpy(&topo_blob[hdr.off_points], &point_row, sizeof(point_row));
  topo_finalize_crc(topo_blob, topo_blob_size);
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_BUDGET, result);

  UT_ASSERT_TRUE(topo_build_runtime_valid_payload(topo_blob, sizeof(topo_blob), &topo_blob_size));
  memcpy(&hdr, topo_blob, sizeof(hdr));
  memcpy(&point_row, &topo_blob[hdr.off_points], sizeof(point_row));
  point_row.point_type = 5U;
  point_row.reg_offset = 8U;
  memcpy(&topo_blob[hdr.off_points], &point_row, sizeof(point_row));
  topo_finalize_crc(topo_blob, topo_blob_size);
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_BOUNDS, result);

  UT_ASSERT_TRUE(topo_build_runtime_valid_payload(topo_blob, sizeof(topo_blob), &topo_blob_size));
  memcpy(&hdr, topo_blob, sizeof(hdr));
  memcpy(&req_row, &topo_blob[hdr.off_requests], sizeof(req_row));
  req_row.period_ms = 10U;
  memcpy(&topo_blob[hdr.off_requests], &req_row, sizeof(req_row));
  topo_finalize_crc(topo_blob, topo_blob_size);
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_BUDGET, result);

  UT_ASSERT_TRUE(topo_build_runtime_valid_payload_with_command(topo_blob, sizeof(topo_blob), &topo_blob_size));
  UT_ASSERT_TRUE(GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_IDLE, result);

  UT_ASSERT_TRUE(topo_build_runtime_valid_payload_with_command(topo_blob, sizeof(topo_blob), &topo_blob_size));
  memcpy(&hdr, topo_blob, sizeof(hdr));
  UT_ASSERT_EQ_U32(1U, hdr.cmd_count);
  UT_ASSERT_TRUE(hdr.off_commands != 0U);
  {
    gh_topology_v2_cmd_t cmd_row = {0};
    memcpy(&cmd_row, &topo_blob[hdr.off_commands], sizeof(cmd_row));
    cmd_row.max_reg_count = (uint16_t)(GH_MB_CMD_PAYLOAD_WORDS + 1U);
    memcpy(&topo_blob[hdr.off_commands], &cmd_row, sizeof(cmd_row));
  }
  topo_finalize_crc(topo_blob, topo_blob_size);
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_BOUNDS, result);

  UT_ASSERT_TRUE(topo_build_runtime_valid_payload_with_command(topo_blob, sizeof(topo_blob), &topo_blob_size));
  memcpy(&hdr, topo_blob, sizeof(hdr));
  {
    gh_topology_v2_cmd_t cmd_row = {0};
    memcpy(&cmd_row, &topo_blob[hdr.off_commands], sizeof(cmd_row));
    cmd_row.ack_point_id = 65535U;
    memcpy(&topo_blob[hdr.off_commands], &cmd_row, sizeof(cmd_row));
  }
  topo_finalize_crc(topo_blob, topo_blob_size);
  UT_ASSERT_TRUE(!GH_TopologyV2_ValidatePayload(topo_blob, topo_blob_size, &result));
  UT_ASSERT_EQ_U32(CFG_RESULT_REJECT_TOPOLOGY_SCHEMA, result);

  return 0;
}
