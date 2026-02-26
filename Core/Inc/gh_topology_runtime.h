#ifndef GH_TOPOLOGY_RUNTIME_H
#define GH_TOPOLOGY_RUNTIME_H

#include "gh_runtime_state.h"

#include <stdbool.h>
#include <stdint.h>

#define GH_TOPOLOGY_DIAG_OFFSET_NONE 0xFFU

typedef struct
{
  uint8_t slave_id;
  uint16_t start_reg;
  uint16_t reg_count;
  uint16_t period_ms;
  uint8_t retries;
  uint8_t backoff_ms;
  uint8_t telemetry_word_count;
  uint8_t diag_offset;
} gh_topology_poll_req_t;

void GH_TopologyRuntime_Clear(void);
bool GH_TopologyRuntime_RebuildFromPayload(const uint8_t *payload, uint32_t payload_len);
bool GH_TopologyRuntime_CopyPollPlan(gh_topology_poll_req_t *out_reqs,
                                     uint16_t max_reqs,
                                     uint16_t *out_count,
                                     uint32_t *out_generation,
                                     uint32_t *out_rtu1_slave_mask);

#endif /* GH_TOPOLOGY_RUNTIME_H */
