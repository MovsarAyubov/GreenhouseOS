#include "gh_config_storage.h"

#include "gh_runtime_state.h"
#include "gh_crc32.h"
#include "gh_modbus_map.h"
#include "gh_topology_v2.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#if !defined(FLASH_TYPEPROGRAM_WORD)
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define FLASH_TYPEPROGRAM_WORD 0U
static void HAL_FLASH_Unlock(void) {}
static void HAL_FLASH_Lock(void) {}
static HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uint32_t Address, uint64_t Data)
{
  (void)TypeProgram;
  (void)Address;
  (void)Data;
  return HAL_OK;
}
#endif

typedef struct
{
  uint32_t start;
  uint32_t end;
} topo_range_t;

typedef struct __attribute__((packed))
{
  uint32_t total_size;
  uint32_t generation;
  uint32_t blob_crc;
  uint32_t valid_marker;
} topology_slot_header_t;

static uint8_t s_topology_active_blob[TOPOLOGY_MAX_BLOB_SIZE];
static uint32_t s_topology_active_size = 0U;
static uint32_t s_topology_active_generation = 0U;
static bool s_topology_active_valid = false;
static bool s_topology_prefer_slot_a = true;

static uint8_t s_topology_staging_blob[TOPOLOGY_MAX_BLOB_SIZE];
static uint32_t s_topology_staging_size = 0U;
static uint32_t s_topology_staging_generation = 0U;
static uint32_t s_topology_staging_chunks_mask = 0U;

static void topo_runtime_clear(void)
{
  g_topology_v2_active = 0U;
  g_topology_v2_ver_major = 0U;
  g_topology_v2_ver_minor = 0U;
  g_topology_v2_generation = 0U;
  g_topology_v2_module_count = 0U;
  g_topology_v2_req_count = 0U;
  g_topology_v2_point_count = 0U;
  g_topology_v2_cmd_count = 0U;
  g_topology_v2_policy_count = 0U;
  g_topology_v2_active_size = 0U;
}

static bool topo_safe_mul_u32(uint32_t a, uint32_t b, uint32_t *out)
{
  if (out == NULL)
  {
    return false;
  }
  if ((a != 0U) && (b > (0xFFFFFFFFUL / a)))
  {
    return false;
  }
  *out = a * b;
  return true;
}

static bool topo_section_bounds(uint32_t off,
                                uint32_t count,
                                uint32_t elem_size,
                                uint32_t total_size,
                                topo_range_t *out_range)
{
  uint32_t bytes;

  if (out_range == NULL)
  {
    return false;
  }

  out_range->start = 0U;
  out_range->end = 0U;

  if (count == 0U)
  {
    return true;
  }

  if ((off < sizeof(gh_topology_v2_header_t)) || (off >= total_size) || ((off & 0x1U) != 0U))
  {
    return false;
  }

  if (!topo_safe_mul_u32(count, elem_size, &bytes) || (bytes == 0U))
  {
    return false;
  }
  if (off > (total_size - bytes))
  {
    return false;
  }

  out_range->start = off;
  out_range->end = off + bytes;
  return true;
}

static bool topo_ranges_overlap(const topo_range_t *a, const topo_range_t *b)
{
  if ((a == NULL) || (b == NULL))
  {
    return false;
  }
  if ((a->end <= a->start) || (b->end <= b->start))
  {
    return false;
  }
  return !((a->end <= b->start) || (b->end <= a->start));
}

bool GH_TopologyV2_IsPayload(const uint8_t *payload, uint32_t payload_len)
{
  uint32_t magic;

  if ((payload == NULL) || (payload_len < sizeof(uint32_t)))
  {
    return false;
  }

  memcpy(&magic, payload, sizeof(magic));
  return (magic == GH_TOPOLOGY_V2_MAGIC);
}

bool GH_TopologyV2_ValidatePayload(const uint8_t *payload,
                                   uint32_t payload_len,
                                   config_result_code_t *out_result)
{
  gh_topology_v2_header_t hdr;
  gh_topology_v2_header_t hdr_crc_copy;
  topo_range_t ranges[5];
  uint8_t i;
  uint8_t j;
  uint32_t body_crc;
  uint32_t header_crc;

  if (out_result == NULL)
  {
    return false;
  }
  *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;

  if ((payload == NULL) || (payload_len < sizeof(hdr)))
  {
    return false;
  }

  memcpy(&hdr, payload, sizeof(hdr));
  if (hdr.magic != GH_TOPOLOGY_V2_MAGIC)
  {
    return false;
  }
  if (hdr.ver_major != GH_TOPOLOGY_V2_VERSION_MAJOR)
  {
    return false;
  }
  if ((hdr.total_size < sizeof(hdr)) || (hdr.total_size > payload_len))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
    return false;
  }

  memcpy(&hdr_crc_copy, &hdr, sizeof(hdr_crc_copy));
  hdr_crc_copy.header_crc32 = 0U;
  header_crc = gh_crc32_compute((const uint8_t *)&hdr_crc_copy, sizeof(hdr_crc_copy));
  body_crc = gh_crc32_compute(&payload[sizeof(hdr)], hdr.total_size - sizeof(hdr));
  if ((header_crc != hdr.header_crc32) || (body_crc != hdr.body_crc32))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_CRC;
    return false;
  }

  if ((hdr.module_count > GH_TOPOLOGY_V2_MAX_MODULES) ||
      (hdr.req_count > GH_TOPOLOGY_V2_MAX_REQ_PROFILES) ||
      (hdr.point_count > GH_TOPOLOGY_V2_MAX_POINTS) ||
      (hdr.cmd_count > GH_TOPOLOGY_V2_MAX_COMMANDS) ||
      (hdr.policy_count > GH_TOPOLOGY_V2_MAX_POLICIES))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_BUDGET;
    return false;
  }

  if (!topo_section_bounds(hdr.off_modules,
                           hdr.module_count,
                           sizeof(gh_topology_v2_module_t),
                           hdr.total_size,
                           &ranges[0]) ||
      !topo_section_bounds(hdr.off_requests,
                           hdr.req_count,
                           sizeof(gh_topology_v2_req_t),
                           hdr.total_size,
                           &ranges[1]) ||
      !topo_section_bounds(hdr.off_points,
                           hdr.point_count,
                           sizeof(gh_topology_v2_point_t),
                           hdr.total_size,
                           &ranges[2]) ||
      !topo_section_bounds(hdr.off_commands,
                           hdr.cmd_count,
                           sizeof(gh_topology_v2_cmd_t),
                           hdr.total_size,
                           &ranges[3]) ||
      !topo_section_bounds(hdr.off_policies,
                           hdr.policy_count,
                           sizeof(gh_topology_v2_policy_t),
                           hdr.total_size,
                           &ranges[4]))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
    return false;
  }

  for (i = 0U; i < 5U; i++)
  {
    for (j = (uint8_t)(i + 1U); j < 5U; j++)
    {
      if (topo_ranges_overlap(&ranges[i], &ranges[j]))
      {
        *out_result = CFG_RESULT_REJECT_TOPOLOGY_COLLISION;
        return false;
      }
    }
  }

  *out_result = CFG_RESULT_IDLE;
  return true;
}

void GH_TopologyV2_SyncRuntimeFromPayload(const uint8_t *payload, uint32_t payload_len)
{
  gh_topology_v2_header_t hdr;
  config_result_code_t result = CFG_RESULT_IDLE;

  topo_runtime_clear();

  if (!GH_TopologyV2_ValidatePayload(payload, payload_len, &result))
  {
    return;
  }

  memcpy(&hdr, payload, sizeof(hdr));
  g_topology_v2_active = 1U;
  g_topology_v2_ver_major = hdr.ver_major;
  g_topology_v2_ver_minor = hdr.ver_minor;
  g_topology_v2_generation = hdr.generation;
  g_topology_v2_module_count = hdr.module_count;
  g_topology_v2_req_count = hdr.req_count;
  g_topology_v2_point_count = hdr.point_count;
  g_topology_v2_cmd_count = hdr.cmd_count;
  g_topology_v2_policy_count = hdr.policy_count;
  g_topology_v2_active_size = hdr.total_size;
}

void GH_TopologyV2_SyncRuntimeFromConfig(const active_config_t *cfg)
{
  if (cfg == NULL)
  {
    topo_runtime_clear();
    return;
  }

  GH_TopologyV2_SyncRuntimeFromPayload(cfg->payload, CONFIG_PAYLOAD_SIZE);
}

static void topology_staging_reset(void)
{
  s_topology_staging_size = 0U;
  s_topology_staging_generation = 0U;
  s_topology_staging_chunks_mask = 0U;
  memset(s_topology_staging_blob, 0, sizeof(s_topology_staging_blob));
}

static uint8_t topology_required_chunks(uint32_t total_size)
{
  if (total_size == 0U)
  {
    return 0U;
  }
  return (uint8_t)((total_size + TOPOLOGY_UPLOAD_CHUNK_BYTES - 1U) / TOPOLOGY_UPLOAD_CHUNK_BYTES);
}

static bool topology_all_chunks_received(uint32_t total_size)
{
  uint8_t required = topology_required_chunks(total_size);
  uint32_t required_mask;

  if (required == 0U)
  {
    return false;
  }
  if (required >= 32U)
  {
    return false;
  }

  required_mask = (1UL << required) - 1UL;
  return ((s_topology_staging_chunks_mask & required_mask) == required_mask);
}

static bool topo_flash_write_words(uint32_t addr, const uint8_t *data, uint32_t len)
{
  uint32_t i;
  uint32_t word;
  HAL_StatusTypeDef st;

  for (i = 0U; i < len; i += 4U)
  {
    word = 0xFFFFFFFFUL;
    memcpy(&word, &data[i], ((len - i) >= 4U) ? 4U : (len - i));
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word);
    if (st != HAL_OK)
    {
      return false;
    }
  }
  return true;
}

static bool topology_write_payload(uint32_t topo_slot_addr,
                                   const uint8_t *blob,
                                   uint32_t total_size,
                                   uint32_t generation)
{
  topology_slot_header_t hdr = {0};
  bool ok;
  uint32_t marker_addr;

  if ((blob == NULL) || (total_size == 0U) || (total_size > TOPOLOGY_MAX_BLOB_SIZE))
  {
    return false;
  }

  hdr.total_size = total_size;
  hdr.generation = generation;
  hdr.blob_crc = gh_crc32_compute(blob, total_size);
  hdr.valid_marker = 0xFFFFFFFFUL;

  HAL_FLASH_Unlock();

  ok = topo_flash_write_words(topo_slot_addr, (const uint8_t *)&hdr, sizeof(hdr));
  ok = ok && topo_flash_write_words(topo_slot_addr + sizeof(hdr), blob, total_size);
  if (ok)
  {
    marker_addr = topo_slot_addr + (uint32_t)offsetof(topology_slot_header_t, valid_marker);
    ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, marker_addr, TOPOLOGY_VALID_MARKER) == HAL_OK);
  }

  HAL_FLASH_Lock();
  return ok;
}

static bool topology_slot_read(uint32_t topo_slot_addr,
                               uint8_t *out_blob,
                               uint32_t *out_size,
                               uint32_t *out_generation)
{
  const topology_slot_header_t *hdr = (const topology_slot_header_t *)topo_slot_addr;
  const uint8_t *blob = (const uint8_t *)(topo_slot_addr + sizeof(topology_slot_header_t));
  config_result_code_t validation_result = CFG_RESULT_IDLE;

  if ((out_blob == NULL) || (out_size == NULL) || (out_generation == NULL))
  {
    return false;
  }

  if (hdr->valid_marker != TOPOLOGY_VALID_MARKER)
  {
    return false;
  }
  if ((hdr->total_size < sizeof(gh_topology_v2_header_t)) || (hdr->total_size > TOPOLOGY_MAX_BLOB_SIZE))
  {
    return false;
  }
  if (gh_crc32_compute(blob, hdr->total_size) != hdr->blob_crc)
  {
    return false;
  }
  if (!GH_TopologyV2_ValidatePayload(blob, hdr->total_size, &validation_result))
  {
    return false;
  }

  memcpy(out_blob, blob, hdr->total_size);
  *out_size = hdr->total_size;
  *out_generation = hdr->generation;
  return true;
}

static bool cfg_write_slot_with_retries(uint32_t slot_addr,
                                        uint32_t sector,
                                        const active_config_t *cfg)
{
  uint8_t attempt;
  bool ok;

  for (attempt = 0U; attempt < CONFIG_FLASH_WRITE_RETRIES; attempt++)
  {
    ok = config_write_to_slot(slot_addr, sector, cfg);
    if (ok && s_topology_active_valid)
    {
      ok = topology_write_payload(slot_addr + TOPOLOGY_SLOT_OFFSET,
                                  s_topology_active_blob,
                                  s_topology_active_size,
                                  s_topology_active_generation);
    }
    if (ok)
    {
      return true;
    }
    if ((attempt + 1U) < CONFIG_FLASH_WRITE_RETRIES)
    {
      osDelay(CONFIG_FLASH_RETRY_DELAY_MS);
    }
  }

  return false;
}

static bool topology_write_slot_with_retries(uint32_t slot_addr,
                                             uint32_t sector,
                                             const active_config_t *legacy_cfg,
                                             const uint8_t *blob,
                                             uint32_t total_size,
                                             uint32_t generation)
{
  uint8_t attempt;
  bool ok;

  for (attempt = 0U; attempt < CONFIG_FLASH_WRITE_RETRIES; attempt++)
  {
    ok = config_write_to_slot(slot_addr, sector, legacy_cfg);
    if (ok)
    {
      ok = topology_write_payload(slot_addr + TOPOLOGY_SLOT_OFFSET, blob, total_size, generation);
    }
    if (ok)
    {
      return true;
    }
    if ((attempt + 1U) < CONFIG_FLASH_WRITE_RETRIES)
    {
      osDelay(CONFIG_FLASH_RETRY_DELAY_MS);
    }
  }

  return false;
}

static bool cfg_store_ab_with_fallback(bool prefer_slot_a,
                                       const active_config_t *cfg,
                                       bool *out_slot_a_used)
{
  bool ok;

  if ((cfg == NULL) || (out_slot_a_used == NULL))
  {
    return false;
  }

  if (prefer_slot_a)
  {
    ok = cfg_write_slot_with_retries(CONFIG_SLOT_A_ADDR, CONFIG_SLOT_A_SECTOR, cfg);
    if (ok)
    {
      *out_slot_a_used = true;
      return true;
    }
    ok = cfg_write_slot_with_retries(CONFIG_SLOT_B_ADDR, CONFIG_SLOT_B_SECTOR, cfg);
    if (ok)
    {
      *out_slot_a_used = false;
      return true;
    }
  }
  else
  {
    ok = cfg_write_slot_with_retries(CONFIG_SLOT_B_ADDR, CONFIG_SLOT_B_SECTOR, cfg);
    if (ok)
    {
      *out_slot_a_used = false;
      return true;
    }
    ok = cfg_write_slot_with_retries(CONFIG_SLOT_A_ADDR, CONFIG_SLOT_A_SECTOR, cfg);
    if (ok)
    {
      *out_slot_a_used = true;
      return true;
    }
  }

  return false;
}

static bool topology_store_ab_with_fallback(bool prefer_slot_a,
                                            const active_config_t *legacy_cfg,
                                            const uint8_t *blob,
                                            uint32_t total_size,
                                            uint32_t generation,
                                            bool *out_slot_a_used)
{
  bool ok;

  if ((legacy_cfg == NULL) || (blob == NULL) || (out_slot_a_used == NULL))
  {
    return false;
  }

  if (prefer_slot_a)
  {
    ok = topology_write_slot_with_retries(CONFIG_SLOT_A_ADDR,
                                          CONFIG_SLOT_A_SECTOR,
                                          legacy_cfg,
                                          blob,
                                          total_size,
                                          generation);
    if (ok)
    {
      *out_slot_a_used = true;
      return true;
    }
    ok = topology_write_slot_with_retries(CONFIG_SLOT_B_ADDR,
                                          CONFIG_SLOT_B_SECTOR,
                                          legacy_cfg,
                                          blob,
                                          total_size,
                                          generation);
    if (ok)
    {
      *out_slot_a_used = false;
      return true;
    }
  }
  else
  {
    ok = topology_write_slot_with_retries(CONFIG_SLOT_B_ADDR,
                                          CONFIG_SLOT_B_SECTOR,
                                          legacy_cfg,
                                          blob,
                                          total_size,
                                          generation);
    if (ok)
    {
      *out_slot_a_used = false;
      return true;
    }
    ok = topology_write_slot_with_retries(CONFIG_SLOT_A_ADDR,
                                          CONFIG_SLOT_A_SECTOR,
                                          legacy_cfg,
                                          blob,
                                          total_size,
                                          generation);
    if (ok)
    {
      *out_slot_a_used = true;
      return true;
    }
  }

  return false;
}

void GH_TopologyStorage_LoadActiveFromFlash(void)
{
  uint8_t blob_a[TOPOLOGY_MAX_BLOB_SIZE];
  uint8_t blob_b[TOPOLOGY_MAX_BLOB_SIZE];
  uint32_t size_a = 0U;
  uint32_t size_b = 0U;
  uint32_t gen_a = 0U;
  uint32_t gen_b = 0U;
  bool valid_a;
  bool valid_b;

  valid_a = topology_slot_read(TOPOLOGY_SLOT_A_ADDR, blob_a, &size_a, &gen_a);
  valid_b = topology_slot_read(TOPOLOGY_SLOT_B_ADDR, blob_b, &size_b, &gen_b);

  if (valid_a && (!valid_b || (gen_a >= gen_b)))
  {
    memcpy(s_topology_active_blob, blob_a, size_a);
    s_topology_active_size = size_a;
    s_topology_active_generation = gen_a;
    s_topology_active_valid = true;
    s_topology_prefer_slot_a = false;
    GH_TopologyV2_SyncRuntimeFromPayload(s_topology_active_blob, s_topology_active_size);
    return;
  }

  if (valid_b)
  {
    memcpy(s_topology_active_blob, blob_b, size_b);
    s_topology_active_size = size_b;
    s_topology_active_generation = gen_b;
    s_topology_active_valid = true;
    s_topology_prefer_slot_a = true;
    GH_TopologyV2_SyncRuntimeFromPayload(s_topology_active_blob, s_topology_active_size);
    return;
  }

  s_topology_active_valid = false;
  s_topology_active_size = 0U;
  s_topology_active_generation = 0U;
  s_topology_prefer_slot_a = true;
  topo_runtime_clear();
}

bool GH_ConfigStorage_PayloadValuesValid(const uint8_t *payload)
{
  uint16_t i;

  for (i = 0U; i < (CONFIG_PAYLOAD_SIZE / sizeof(float)); i++)
  {
    float v;
    memcpy(&v, &payload[i * sizeof(float)], sizeof(float));
    if (!isfinite(v) || (v < -100.0f) || (v > 1000.0f))
    {
      return false;
    }
  }

  return true;
}

bool GH_ConfigStorage_ValidateRequest(const config_update_req_t *req,
                                      config_result_code_t *out_result)
{
  uint32_t crc;

  if ((req == NULL) || (out_result == NULL))
  {
    return false;
  }

  if (req->version <= g_active_config.version)
  {
    *out_result = CFG_RESULT_REJECT_BAD_VERSION;
    return false;
  }

  crc = gh_crc32_compute(req->payload, CONFIG_PAYLOAD_SIZE);
  if (crc != req->payload_crc)
  {
    *out_result = CFG_RESULT_REJECT_BAD_CRC;
    return false;
  }

  if (GH_TopologyV2_IsPayload(req->payload, CONFIG_PAYLOAD_SIZE))
  {
    *out_result = CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
    return false;
  }

  if (!GH_ConfigStorage_PayloadValuesValid(req->payload))
  {
    *out_result = CFG_RESULT_REJECT_RANGE;
    return false;
  }

  *out_result = CFG_RESULT_IDLE;
  return true;
}

static config_result_code_t topology_process_chunk(const topology_chunk_req_t *req)
{
  uint8_t chunk_bytes[TOPOLOGY_UPLOAD_CHUNK_BYTES];
  uint32_t chunk_bytes_len = 0U;
  uint32_t chunk_offset;
  uint32_t chunk_crc;
  uint8_t required_chunks;
  uint16_t i;
  config_result_code_t validation_result = CFG_RESULT_IDLE;
  bool written_slot_a = false;
  bool ok;

  if (req == NULL)
  {
    return CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
  }

  if ((req->flags & TOPOLOGY_UPLOAD_FLAG_RESET) != 0U)
  {
    topology_staging_reset();
  }

  if ((req->total_size < sizeof(gh_topology_v2_header_t)) || (req->total_size > TOPOLOGY_MAX_BLOB_SIZE))
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BUDGET;
  }

  required_chunks = topology_required_chunks(req->total_size);
  if ((required_chunks == 0U) || (required_chunks >= 32U))
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BUDGET;
  }
  if (req->chunk_index >= required_chunks)
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
  }
  if (req->chunk_words > TOPOLOGY_UPLOAD_CHUNK_WORDS)
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
  }

  if ((s_topology_staging_size == 0U) ||
      (s_topology_staging_size != req->total_size) ||
      (s_topology_staging_generation != req->generation))
  {
    if (((req->flags & TOPOLOGY_UPLOAD_FLAG_RESET) == 0U) && (req->chunk_index != 0U))
    {
      return CFG_RESULT_REJECT_TOPOLOGY_SCHEMA;
    }
    topology_staging_reset();
    s_topology_staging_size = req->total_size;
    s_topology_staging_generation = req->generation;
  }

  chunk_bytes_len = (uint32_t)req->chunk_words * 2U;
  chunk_offset = (uint32_t)req->chunk_index * TOPOLOGY_UPLOAD_CHUNK_BYTES;
  if ((chunk_offset + chunk_bytes_len) > req->total_size)
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
  }

  for (i = 0U; i < req->chunk_words; i++)
  {
    chunk_bytes[2U * i] = (uint8_t)(req->chunk_data[i] >> 8U);
    chunk_bytes[(2U * i) + 1U] = (uint8_t)(req->chunk_data[i] & 0x00FFU);
  }

  if (chunk_bytes_len > 0U)
  {
    chunk_crc = gh_crc32_compute(chunk_bytes, chunk_bytes_len);
    if (chunk_crc != req->chunk_crc)
    {
      return CFG_RESULT_REJECT_TOPOLOGY_CRC;
    }

    memcpy(&s_topology_staging_blob[chunk_offset], chunk_bytes, chunk_bytes_len);
    s_topology_staging_chunks_mask |= (1UL << req->chunk_index);
  }

  if ((req->flags & TOPOLOGY_UPLOAD_FLAG_COMMIT) == 0U)
  {
    return CFG_RESULT_QUEUED;
  }

  if (!topology_all_chunks_received(req->total_size))
  {
    return CFG_RESULT_REJECT_TOPOLOGY_BOUNDS;
  }

  if (!GH_TopologyV2_ValidatePayload(s_topology_staging_blob, req->total_size, &validation_result))
  {
    return validation_result;
  }

  ok = topology_store_ab_with_fallback(s_topology_prefer_slot_a,
                                       &g_active_config,
                                       s_topology_staging_blob,
                                       req->total_size,
                                       req->generation,
                                       &written_slot_a);
  if (!ok)
  {
    return CFG_RESULT_FLASH_FAIL;
  }

  s_topology_prefer_slot_a = !written_slot_a;
  memcpy(s_topology_active_blob, s_topology_staging_blob, req->total_size);
  s_topology_active_size = req->total_size;
  s_topology_active_generation = req->generation;
  s_topology_active_valid = true;
  GH_TopologyV2_SyncRuntimeFromPayload(s_topology_active_blob, s_topology_active_size);
  topology_staging_reset();

  return CFG_RESULT_APPLIED;
}

void GH_ConfigStorageTask_Run(void *argument)
{
  config_update_req_t req = {0};
  topology_chunk_req_t topo_req = {0};
  config_apply_req_t apply_req = {0};
  active_config_t pending = {0};
  bool prefer_slot_a = true;
  bool written_slot_a = false;
  bool ok;
  uint8_t attempt;
  config_result_code_t validation_result = CFG_RESULT_IDLE;
  config_result_code_t topo_result = CFG_RESULT_IDLE;
  (void)argument;

  for (;;)
  {
    if (osMessageQueueGet(qTopologyStoreHandle, &topo_req, NULL, 0U) == osOK)
    {
      topo_result = topology_process_chunk(&topo_req);
      GH_ModbusMap_ReportTopologyResult(topo_req.request_token,
                                        topo_result,
                                        g_topology_v2_generation,
                                        g_topology_v2_active_size);

      if ((topo_result != CFG_RESULT_QUEUED) && (topo_result != CFG_RESULT_APPLIED))
      {
        publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)topo_result);
      }
      else if (topo_result == CFG_RESULT_APPLIED)
      {
        publish_event(EVENT_SEV_INFO, EVENT_CODE_CFG_APPLIED, 0U, (float)g_topology_v2_generation);
      }
    }

    if (osMessageQueueGet(qConfigStoreHandle, &req, NULL, 100U) == osOK)
    {
      pending.version = req.version;
      pending.crc = req.payload_crc;
      memcpy(pending.payload, req.payload, CONFIG_PAYLOAD_SIZE);

      if (!GH_ConfigStorage_ValidateRequest(&req, &validation_result))
      {
        GH_ModbusMap_ReportConfigResult(req.request_token, validation_result, g_active_config.version);
        publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)validation_result);
        g_setpoints_apply_in_progress = false;
        continue;
      }

      ok = cfg_store_ab_with_fallback(prefer_slot_a, &pending, &written_slot_a);

      if (ok)
      {
        g_status.flash_write_ok_count++;
        prefer_slot_a = !written_slot_a;

        apply_req.request_token = req.request_token;
        apply_req.reserved0 = 0U;
        apply_req.config = pending;

        ok = false;
        for (attempt = 0U; attempt < CONFIG_APPLY_QUEUE_RETRIES; attempt++)
        {
          if (osMessageQueuePut(qConfigApplyHandle, &apply_req, 0U, 0U) == osOK)
          {
            ok = true;
            break;
          }
          if ((attempt + 1U) < CONFIG_APPLY_QUEUE_RETRIES)
          {
            osDelay(CONFIG_APPLY_QUEUE_DELAY_MS);
          }
        }

        if (!ok)
        {
          GH_ModbusMap_ReportConfigResult(req.request_token,
                                          CFG_RESULT_APPLY_QUEUE_FAIL,
                                          g_active_config.version);
          publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_APPLY_QUEUE_FAIL);
          g_setpoints_apply_in_progress = false;
        }
      }
      else
      {
        g_status.flash_write_fail_count++;
        GH_ModbusMap_ReportConfigResult(req.request_token, CFG_RESULT_FLASH_FAIL, g_active_config.version);
        publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)CFG_RESULT_FLASH_FAIL);
        g_setpoints_apply_in_progress = false;
      }
    }

    task_heartbeat_kick(TASK_BIT_CONFIG);
    osDelay(20U);
  }
}
