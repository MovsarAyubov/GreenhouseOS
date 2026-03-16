# Topology Config v2 (Data-Driven)

Date: 2026-02-25  
Target MCU baseline: STM32F407VET6 (single core, FreeRTOS, Modbus TCP 502 + Modbus RTU master)

## 1. Purpose

`topology_config v2` describes greenhouse topology in data form (not hardcoded logic):

- module types and instances
- polling profiles per module
- point mapping (raw registers -> engineering values)
- command profiles
- safety/fallback policies

This allows integrating different greenhouse layouts with minimal firmware changes.

## 2. Practical limits for STM32F407 baseline

Recommended starting limits (safe for current memory profile):

- `MAX_MODULES = 48`
- `MAX_REQ_PROFILES = 192`
- `MAX_POINTS = 640`
- `MAX_COMMANDS = 192`
- `MAX_FAULT_POLICIES = 64`

Notes:

- Current firmware uses ~95 KB static RAM (`data+bss`) and has ~35 KB linker headroom in RAM.
- FreeRTOS heap is `48 KB` (`configTOTAL_HEAP_SIZE=49152`).
- 64 KB CCMRAM is currently unused and should be used for large runtime registries (non-DMA data).

## 3. Versioning and safety model

- Strict schema versioning: `major.minor`.
- Backward policy:
  - same major, higher minor: accepted if unknown flags are zero.
  - different major: rejected.
- Integrity:
  - CRC32 of body.
  - CRC32 of header.
- Atomic activation:
  - A/B slots in flash.
  - `pending -> validated -> active` state.
  - rollback to previous active on any boot/apply failure.

## 4. Binary layout

```c
typedef struct __attribute__((packed)) {
  uint32_t magic;              // 'TOP2' = 0x324F5054
  uint16_t ver_major;          // 2
  uint16_t ver_minor;          // 0
  uint32_t total_size;         // bytes, full blob
  uint32_t generation;         // monotonic, increment on each accepted config
  uint32_t topology_id;        // deployment/site ID
  uint32_t created_unix_s;     // timestamp from tool
  uint32_t flags;              // reserved global feature flags
  uint16_t module_count;
  uint16_t req_count;
  uint16_t point_count;
  uint16_t cmd_count;
  uint16_t policy_count;
  uint16_t reserved0;
  uint32_t off_modules;        // offset from blob start
  uint32_t off_requests;
  uint32_t off_points;
  uint32_t off_commands;
  uint32_t off_policies;
  uint32_t body_crc32;         // CRC over [first table .. end]
  uint32_t header_crc32;       // CRC over header with this field zeroed
} topo_header_v2_t;
```

## 5. Core enums

```c
typedef enum {
  MOD_TYPE_ZONE_CTRL        = 1, // zone sensors + actuators
  MOD_TYPE_WEATHER          = 2, // meteo station
  MOD_TYPE_MIXER            = 3, // nutrient mixer
  MOD_TYPE_RESERVOIR_OSMO   = 4, // reservoir + osmosis
  MOD_TYPE_STORAGE_ABK      = 5  // storage/ABK climate
} module_type_t;

typedef enum {
  BUS_RTU1 = 1,
  BUS_RTU2 = 2,
  BUS_TCP  = 3
} bus_type_t;

typedef enum {
  FC_READ_HOLDING = 3,
  FC_READ_INPUT   = 4,
  FC_WRITE_SINGLE = 6,
  FC_WRITE_MULTI  = 16
} mb_fc_t;

typedef enum {
  PT_U16   = 1,
  PT_S16   = 2,
  PT_U32   = 3,
  PT_S32   = 4,
  PT_FLOAT = 5,
  PT_BIT   = 6
} point_type_t;
```

Temporary implementation note:
- `BUS_RTU2 (2)` is reserved in schema but currently rejected by both firmware validator and host `topology_packer`.
- Accepted bus types in current release: `BUS_RTU1 (1)` and `BUS_TCP (3)`.

## 6. Tables

### 6.1 Module table

```c
typedef struct __attribute__((packed)) {
  uint16_t module_id;          // unique logical ID
  uint8_t  module_type;        // module_type_t
  uint8_t  bus_type;           // bus_type_t
  uint8_t  bus_index;          // e.g. RS485 port 0/1
  uint8_t  slave_id;           // 1..247 for RTU, 0 for TCP-native
  uint16_t zone_id;            // 0xFFFF if not zone-bound
  uint16_t req_first;          // index in request table
  uint16_t req_count;
  uint16_t cmd_first;          // index in command table
  uint16_t cmd_count;
  uint16_t offline_reprobe_ms;
  uint16_t heartbeat_timeout_ms;
  uint32_t capability_mask;    // module features
  uint32_t user_param0;        // type-specific
  uint32_t user_param1;        // type-specific
} topo_module_v2_t;
```

Authoring contract for module fields is defined inline in this document and in the
runtime validator/loader implementation (`Core/Src/gh_config_storage.c`).

Validation:

- `module_id` unique.
- `(bus_type,bus_index,slave_id)` unique for RTU.
- `req_first+req_count` and `cmd_first+cmd_count` in bounds.
- current runtime allows only `bus_type=1` and `bus_type=3`; `bus_type=2` is rejected until RTU2 routing is implemented.

### 6.2 Request profile table

```c
typedef struct __attribute__((packed)) {
  uint16_t req_id;             // unique
  uint16_t module_id;          // owner
  uint8_t  fc;                 // 3/4 (read profiles)
  uint8_t  priority;           // 0..3
  uint16_t start_reg;
  uint16_t reg_count;          // <= 125 for FC3/4
  uint16_t period_ms;          // polling period
  uint16_t timeout_ms;         // response timeout
  uint8_t  retries;
  uint8_t  backoff_ms;
  uint16_t point_first;        // index in point table
  uint16_t point_count;
  uint16_t flags;              // atomic/snapshot/etc.
} topo_req_profile_v2_t;
```

Validation:

- no overlapping request windows for same `(bus,slave,period class)` unless explicitly allowed flag.
- timeout/retry within deterministic budget.

### 6.3 Point table

```c
typedef struct __attribute__((packed)) {
  uint16_t point_id;           // unique global point ID
  uint16_t module_id;
  uint16_t req_id;
  uint16_t reg_offset;         // inside request response
  uint8_t  point_type;         // point_type_t
  int8_t   scale_pow10;        // engineering scaling
  uint8_t  bit_index;          // for PT_BIT
  uint8_t  quality_policy;     // stale/fault rules profile
  uint16_t publish_index;      // exported index in unified map
  uint16_t stale_timeout_s;
  int16_t  alarm_low;          // optional, type-dependent
  int16_t  alarm_high;         // optional, type-dependent
} topo_point_v2_t;
```

Validation:

- `point_id` unique.
- `publish_index` unique.
- `reg_offset < reg_count` of referenced request.
- current runtime publishes points into `g_sensors[publish_index]`, so `publish_index < SENSOR_COUNT` is required.

### 6.4 Command table

```c
typedef struct __attribute__((packed)) {
  uint16_t cmd_id;             // unique
  uint16_t module_id;
  uint8_t  fc;                 // 6 or 16
  uint8_t  retries;
  uint16_t start_reg;
  uint16_t max_reg_count;
  uint16_t payload_offset;     // offset inside ingress payload buffer
  uint16_t timeout_ms;
  uint16_t ack_point_id;       // optional acknowledgement point
  uint16_t flags;              // bit[15:14]=cmd_kind, other bits reserved for command options
} topo_cmd_v2_t;
```

Current firmware constraints for RTU1 command execution:
- `payload_offset + max_reg_count <= 16` (command ingress payload budget per request).
- `fc=6` requires `max_reg_count == 1`.
- `ack_point_id` (if non-zero) must reference existing `point_id` from the same module.
- `cmd_kind` is encoded in `flags[15:14]`:
  - `0`: generic single-step command profile
  - `1`: schedule command step (executed by generic dispatcher as contiguous `payload_offset` chain)
- for `cmd_kind=schedule` and `fc=16`, `max_reg_count` must be `12` (`4` slots x `3` words).

### 6.5 Fault policy table

```c
typedef struct __attribute__((packed)) {
  uint16_t module_id;
  uint8_t  on_timeout;         // keep_last / safe_default / force_offline
  uint8_t  on_crc_error;       // action enum
  uint8_t  on_link_loss;       // action enum
  uint8_t  reserved0;
  uint16_t max_consecutive_fail;
  uint16_t recover_good_cycles;
  uint16_t safe_profile_id;    // optional predefined safe setpoint profile
  uint16_t reserved1;
} topo_fault_policy_v2_t;
```

Current action enum used by firmware:
- `0`: `keep_last`
- `1`: `safe_default` (marks points as `FAULT`)
- `2`: `force_offline` (marks points as `OFFLINE`)

Current runtime behavior:
- `on_timeout` and `on_crc_error` are selected from RTU read error classification.
- `on_link_loss` is selected when `comm_fail_streak >= max_consecutive_fail` (or default offline threshold if `0`).
- `recover_good_cycles` overrides recovery hysteresis for returning to `QUALITY_OK`.
- `safe_profile_id` is accepted/validated and preserved, but not yet bound to automatic writeback profile execution.

## 7. Runtime contracts

- Poll engine works only from request profiles.
- No hardcoded per-slave read map in source.
- Each point published with:
  - `value`
  - `quality`
  - `timestamp_ms`
  - `source module_id`
- TCP client reads directory and point blocks, not fixed old offsets.

## 8. Transport and storage

`CONFIG_PAYLOAD_SIZE=128` is not enough for topology v2 and remains only for legacy config payload.

Implemented baseline:

- chunked upload via dedicated Modbus topology window (`GH_MB_TOPO_BASE`, `GH_MB_TOPO_REGS`)
- per-chunk metadata:
  - `chunk_index`
  - `chunk_words`
  - `total_size`
  - `chunk_crc32`
  - `generation`
  - `flags` (`RESET_STAGING`, `COMMIT`)
- explicit submit trigger by changing non-zero `SUBMIT_TOKEN`
- dedicated topology A/B persistence in flash (kept together with legacy slot writes)
- full payload validation before activation:
  - schema/version
  - bounds
  - section overlap
  - CRCs
  - budget checks

Client integration sequence is specified in `docs/topology_upload_protocol.md`.

## 9. Industrial reliability requirements

Mandatory acceptance checks before activation:

- schema/version validity
- bounds and uniqueness checks
- deterministic poll budget check against watchdog budget
- memory budget check (all pools fit)
- dry-run scheduler build

If any check fails:

- reject with explicit error code
- keep previous active topology
- publish diagnostic event (`CFG_REJECTED` + reject reason)

## 10. Hardware verdict for this architecture

For this project profile, STM32F407 is still viable if:

- no TLS/web UI/heavy analytics on device
- moderate TCP clients (SCADA/HMI class, not dozens)
- deterministic polling and static memory pools
- use CCMRAM for large registries

Dual-core MCU is not required just to split TCP/RTU:

- FreeRTOS task separation already solves this deterministically.
- Main bottleneck is usually bus/timeout policy and memory architecture, not core count.

Upgrade MCU (e.g. STM32H7 class) is justified if planned scope includes:

- significantly larger point count (>1000-1500),
- multiple heavy protocols concurrently,
- TLS/PKI, OTA with compression, local historian,
- strict low-latency control under high network load.
