# Topology Module Contract v1

Date: `2026-02-26`
Status: `Approved for host-tooling and topology authoring`

This document defines strict authoring rules for these fields in `topology_config v2`:
- `module_id`
- `module_type`
- `bus_type`
- `bus_index`
- `cmd_first`
- `cmd_count`
- `capability_mask`
- `offline_reprobe_ms`
- `user_param0`
- `user_param1`

Note:
- Current firmware fully validates/stores topology and exposes active metadata.
- Detailed runtime behavior based on all module table fields is part of the next data-driven runtime stage.
- This contract is needed now to keep topology files consistent across projects.

## 1. Module Type Enum

Fixed values:
- `1` = `MOD_TYPE_ZONE_CTRL`
- `2` = `MOD_TYPE_WEATHER`
- `3` = `MOD_TYPE_MIXER`
- `4` = `MOD_TYPE_RESERVOIR_OSMO`
- `5` = `MOD_TYPE_STORAGE_ABK`

## 2. Module ID Plan

Use deterministic ranges:
- `100..199`: zone controllers
- `200..299`: weather modules
- `300..399`: mixer modules
- `400..499`: reservoir/osmosis modules
- `500..599`: storage/ABK modules

Rules:
- `module_id` must be unique in one topology.
- `module_id` is a logical ID, not required to equal `slave_id`.

## 3. Bus Mapping Rules

`bus_type`:
- `1` = `BUS_RTU1`
- `2` = `BUS_RTU2`
- `3` = `BUS_TCP`

`bus_index`:
- For RTU: physical line index, typically `0` (line A) or `1` (line B).
- For TCP-native modules: set `0`.

`slave_id`:
- For RTU modules: `1..247`.
- For TCP-native modules: `0`.

## 4. Command Range Ownership

`cmd_first` and `cmd_count` define which entries in `commands[]` belong to module.

Rules:
- `cmd_first` is index of first command row for this module.
- `cmd_count` is number of contiguous rows.
- If module has no commands: `cmd_first=0`, `cmd_count=0`.
- Recommended ordering in `commands[]`: first by `module_id`, then by `cmd_id`.

Authoring checklist:
1. Build `commands[]`.
2. For each module, find first matching index.
3. Count contiguous matching rows.
4. Write `cmd_first/cmd_count`.

## 5. capability_mask Contract

### 5.1 Common bits (all module types)

- bit `0`: `CAP_TELEMETRY_READ` (supports read profiles)
- bit `1`: `CAP_COMMAND_WRITE` (supports write commands)
- bit `2`: `CAP_HEARTBEAT` (supports heartbeat supervision)
- bit `3`: `CAP_FW_VERSION_REG` (firmware/version register exists)
- bit `4`: `CAP_SAFE_PROFILE` (safe profile available)
- bit `5`: `CAP_REMOTE_RESET` (remote reset command supported)
- bit `6`: `CAP_DIAG_REGS` (diagnostic registers available)
- bit `7`: `CAP_LOCAL_AUTONOMY` (module can switch to autonomous mode)
- bit `8`: `CAP_ACK_POINT` (command acknowledge point available)
- bit `9`: `CAP_ALARM_BITS` (alarm bitfield exposed)
- bit `10`: `CAP_BATCH_WRITE_FC16`
- bit `11`: `CAP_SINGLE_WRITE_FC6`

Bits `12..15`: reserved.

### 5.2 Type-specific bits (valid only for matching module_type)

Bits `16..23`:

For `MOD_TYPE_ZONE_CTRL`:
- bit `16`: temperature sensor
- bit `17`: humidity sensor
- bit `18`: CO2 sensor
- bit `19`: vent actuator
- bit `20`: fan actuator
- bit `21`: heater actuator
- bit `22`: irrigation valve

For `MOD_TYPE_WEATHER`:
- bit `16`: outside temperature
- bit `17`: outside humidity
- bit `18`: wind speed
- bit `19`: wind direction
- bit `20`: rain sensor
- bit `21`: solar radiation
- bit `22`: barometric pressure

For `MOD_TYPE_MIXER`:
- bit `16`: EC sensor
- bit `17`: pH sensor
- bit `18`: doser A
- bit `19`: doser B
- bit `20`: doser C
- bit `21`: circulation pump
- bit `22`: flush cycle

For `MOD_TYPE_RESERVOIR_OSMO`:
- bit `16`: level sensor
- bit `17`: osmosis state
- bit `18`: TDS sensor
- bit `19`: pressure sensor
- bit `20`: inlet valve
- bit `21`: outlet valve
- bit `22`: leak sensor

For `MOD_TYPE_STORAGE_ABK`:
- bit `16`: room temperature
- bit `17`: room humidity
- bit `18`: compressor
- bit `19`: defrost
- bit `20`: door sensor
- bit `21`: HVAC fan
- bit `22`: heating/cooling relay

Bits `24..31`: reserved for future global extensions.

## 6. user_param0 / user_param1 Contract

### 6.1 user_param0 (uniform identity packing)

`user_param0` is packed as:
- bits `0..15`: `local_module_no`
- bits `16..23`: `greenhouse_no`
- bits `24..31`: `site_no`

Formula:
`user_param0 = local_module_no | (greenhouse_no << 16) | (site_no << 24)`

### 6.2 user_param1 (uniform profile packing)

`user_param1` is packed as:
- bits `0..7`: `profile_id`
- bits `8..15`: `safety_profile_id`
- bits `16..23`: `publish_group_id`
- bits `24..31`: reserved (`0`)

Formula:
`user_param1 = profile_id | (safety_profile_id << 8) | (publish_group_id << 16)`

Profile meaning by module type:
- Zone: climate profile ID
- Weather: sensor package profile
- Mixer: recipe/control profile
- Reservoir: water-process profile
- Storage: room-climate profile

## 7. offline_reprobe_ms Baseline

Recommended defaults:
- Zone: `30000`
- Weather: `45000`
- Mixer: `10000`
- Reservoir/Osmosis: `15000`
- Storage/ABK: `30000`

Allowed range: `1000..600000` ms.

## 8. Example

Example module row:

- `module_id=101`
- `module_type=1` (zone)
- `bus_type=1`, `bus_index=0`, `slave_id=1`
- `cmd_first=0`, `cmd_count=2`
- `capability_mask=0x007F0B` (example only)
- `user_param0=0x05010001` (`site=5`, `greenhouse=1`, `local_module_no=1`)
- `user_param1=0x00020304` (`profile=4`, `safety=3`, `publish_group=2`)

Use this contract consistently for all greenhouse deployments.
