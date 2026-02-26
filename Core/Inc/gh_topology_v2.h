#ifndef GH_TOPOLOGY_V2_H
#define GH_TOPOLOGY_V2_H

#include "gh_runtime_state.h"

#include <stdbool.h>
#include <stdint.h>

#define GH_TOPOLOGY_V2_MAGIC             0x324F5054UL /* "TOP2" */
#define GH_TOPOLOGY_V2_VERSION_MAJOR     2U

#define GH_TOPOLOGY_V2_MAX_MODULES       48U
#define GH_TOPOLOGY_V2_MAX_REQ_PROFILES  192U
#define GH_TOPOLOGY_V2_MAX_POINTS        640U
#define GH_TOPOLOGY_V2_MAX_COMMANDS      192U
#define GH_TOPOLOGY_V2_MAX_POLICIES      64U

typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint16_t ver_major;
  uint16_t ver_minor;
  uint32_t total_size;
  uint32_t generation;
  uint32_t topology_id;
  uint32_t created_unix_s;
  uint32_t flags;
  uint16_t module_count;
  uint16_t req_count;
  uint16_t point_count;
  uint16_t cmd_count;
  uint16_t policy_count;
  uint16_t reserved0;
  uint32_t off_modules;
  uint32_t off_requests;
  uint32_t off_points;
  uint32_t off_commands;
  uint32_t off_policies;
  uint32_t body_crc32;
  uint32_t header_crc32;
} gh_topology_v2_header_t;

typedef struct __attribute__((packed))
{
  uint16_t module_id;
  uint8_t module_type;
  uint8_t bus_type;
  uint8_t bus_index;
  uint8_t slave_id;
  uint16_t zone_id;
  uint16_t req_first;
  uint16_t req_count;
  uint16_t cmd_first;
  uint16_t cmd_count;
  uint16_t offline_reprobe_ms;
  uint16_t heartbeat_timeout_ms;
  uint32_t capability_mask;
  uint32_t user_param0;
  uint32_t user_param1;
} gh_topology_v2_module_t;

typedef struct __attribute__((packed))
{
  uint16_t req_id;
  uint16_t module_id;
  uint8_t fc;
  uint8_t priority;
  uint16_t start_reg;
  uint16_t reg_count;
  uint16_t period_ms;
  uint16_t timeout_ms;
  uint8_t retries;
  uint8_t backoff_ms;
  uint16_t point_first;
  uint16_t point_count;
  uint16_t flags;
} gh_topology_v2_req_t;

typedef struct __attribute__((packed))
{
  uint16_t point_id;
  uint16_t module_id;
  uint16_t req_id;
  uint16_t reg_offset;
  uint8_t point_type;
  int8_t scale_pow10;
  uint8_t bit_index;
  uint8_t quality_policy;
  uint16_t publish_index;
  uint16_t stale_timeout_s;
  int16_t alarm_low;
  int16_t alarm_high;
} gh_topology_v2_point_t;

typedef struct __attribute__((packed))
{
  uint16_t cmd_id;
  uint16_t module_id;
  uint8_t fc;
  uint8_t retries;
  uint16_t start_reg;
  uint16_t max_reg_count;
  uint16_t timeout_ms;
  uint16_t ack_point_id;
  uint16_t flags;
} gh_topology_v2_cmd_t;

typedef struct __attribute__((packed))
{
  uint16_t module_id;
  uint8_t on_timeout;
  uint8_t on_crc_error;
  uint8_t on_link_loss;
  uint8_t reserved0;
  uint16_t max_consecutive_fail;
  uint16_t recover_good_cycles;
  uint16_t safe_profile_id;
  uint16_t reserved1;
} gh_topology_v2_policy_t;

bool GH_TopologyV2_IsPayload(const uint8_t *payload, uint32_t payload_len);
bool GH_TopologyV2_ValidatePayload(const uint8_t *payload,
                                   uint32_t payload_len,
                                   config_result_code_t *out_result);
void GH_TopologyV2_SyncRuntimeFromPayload(const uint8_t *payload, uint32_t payload_len);
void GH_TopologyV2_SyncRuntimeFromConfig(const active_config_t *cfg);

#endif /* GH_TOPOLOGY_V2_H */
