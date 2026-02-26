#ifndef GH_TOPOLOGY_RUNTIME_H
#define GH_TOPOLOGY_RUNTIME_H

#include "gh_runtime_state.h"

#include <stdbool.h>
#include <stdint.h>

#define GH_TOPOLOGY_DIAG_OFFSET_NONE 0xFFU

typedef struct
{
  uint16_t req_id;
  uint16_t module_id;
  uint8_t slave_id;
  uint16_t start_reg;
  uint16_t reg_count;
  uint16_t period_ms;
  uint16_t timeout_ms;
  uint8_t retries;
  uint8_t backoff_ms;
  uint8_t telemetry_word_count;
  uint8_t diag_offset;
  uint16_t point_first;
  uint16_t point_count;
} gh_topology_poll_req_t;

typedef struct
{
  uint8_t slave_id;
  uint16_t point_id;
  uint16_t req_id;
  uint16_t module_id;
  uint16_t req_start_reg;
  uint16_t reg_offset;
  uint8_t point_type;
  int8_t scale_pow10;
  uint8_t bit_index;
  uint8_t quality_policy;
  uint16_t publish_index;
} gh_topology_point_binding_t;

typedef struct
{
  uint16_t cmd_id;
  uint16_t module_id;
  uint8_t slave_id;
  uint8_t fc;
  uint16_t start_reg;
  uint16_t reg_count;
  uint16_t timeout_ms;
  uint8_t retries;
  uint16_t ack_point_id;
  uint16_t flags;
} gh_topology_cmd_binding_t;

void GH_TopologyRuntime_Clear(void);
bool GH_TopologyRuntime_RebuildFromPayload(const uint8_t *payload, uint32_t payload_len);
bool GH_TopologyRuntime_CopyPollPlan(gh_topology_poll_req_t *out_reqs,
                                     uint16_t max_reqs,
                                     uint16_t *out_count,
                                     uint32_t *out_generation,
                                     uint32_t *out_rtu1_slave_mask);
bool GH_TopologyRuntime_CopyPointBindings(gh_topology_point_binding_t *out_points,
                                          uint16_t max_points,
                                          uint16_t *out_count,
                                          uint32_t *out_generation);
bool GH_TopologyRuntime_CopyCommandBindings(gh_topology_cmd_binding_t *out_cmds,
                                            uint16_t max_cmds,
                                            uint16_t *out_count,
                                            uint32_t *out_generation);

#endif /* GH_TOPOLOGY_RUNTIME_H */
