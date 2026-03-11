# Greenhouse Profile v1

Date: `2026-03-08`.

## Purpose

`greenhouse_profile_v1` is a human-friendly input format for generating `topology_config v2`.

The profile is compiled by:

- `tools/topology_designer/cli.py`

and then can be packed/uploaded with existing tooling:

- `tools/topology/topology_packer.py`
- `tools/topology/topology_uploader.py`

## Scope of v1

This first profile version targets **zone RTU modules on RTU1**:

- one profile zone -> one `modules[]` row (`module_type=1`, `bus_type=1`)
- one profile zone -> one `requests[]` row (`fc=3`)
- selected channels -> `points[]` rows
- one policy per zone -> `policies[]` row

`commands[]` is empty in v1.

## Root schema

Required root fields:

- `schema` = `"greenhouse_profile_v1"`
- `generation` (u32, `>=1`)
- `topology_id` (u32, `>=1`)
- `zones[]` (non-empty list)

Optional root fields:

- `ver_minor` (u16, default `0`)
- `created_unix_s` (u32, default current UNIX time)
- `flags` (u32, default `0`)
- `site_no` (`0..255`, default `1`)
- `greenhouse_no` (`0..255`, default `1`)

## Zone fields

Required zone fields:

- `zone_no` (`1..99`)
- `slave_id` (`1..20`)
- `channels[]` (non-empty list of allowed channel names)

Optional zone fields:

- `bus_index` (`0..1`, default `0`)
- `poll_period_ms` (`100..600000`, default `5000`)
- `timeout_ms` (`50..10000`, default `300`)
- `retries` (`1..10`, default `2`)
- `backoff_ms` (`0..2000`, default `20`)
- `start_reg` (must be `0` in v1)
- `priority` (`0..3`, default `1`)
- `offline_reprobe_ms` (`1000..600000`, default `30000`)
- `heartbeat_timeout_ms` (`100..600000`, default `5000`)
- `quality_policy` (`0..255`, default `1`)
- `stale_timeout_s` (`1..65535`, default `20`)
- `capability_mask` (u32, default `0x00000005`)
- `profile_id` (`0..255`, default `0`)
- `safety_profile_id` (`0..255`, default `0`)
- `publish_group_id` (`0..255`, default `0`)
- `policy_on_timeout` (`0..2`, default `1`)
- `policy_on_crc_error` (`0..2`, default `1`)
- `policy_on_link_loss` (`0..2`, default `2`)
- `policy_max_consecutive_fail` (`0..65535`, default `3`)
- `policy_recover_good_cycles` (`0..65535`, default `2`)
- `policy_safe_profile_id` (`0..65535`, default `1`)

## Allowed channel names

- `AIR_TEMP`
- `AIR_HUM`
- `WATER_RAIL`
- `WATER_GROW`
- `WATER_UNDERTRAY`
- `WATER_UPPER_HEAT`
- `WINDOWS_POS_A`
- `WINDOWS_POS_B`
- `CURTAIN_POS`

## Generated mapping rules

- `module_id = 100 + zone_no` (zone module range `100..199`)
- `req_id = module_id * 10`
- `point_id = module_id * 100 + channel_index`
- `publish_index = (slave_id - 1) * 9 + channel_index`
- channels are sorted by fixed channel order before compile

## Constraints mirrored from firmware/runtime

- `slave_id <= 20`
- `publish_index < 180`
- total zones/points must fit topology v2 budgets
- poll utilization must be `<= 1000` permille

## Example

```json
{
  "schema": "greenhouse_profile_v1",
  "generation": 10,
  "topology_id": 1001,
  "created_unix_s": 1772064000,
  "site_no": 5,
  "greenhouse_no": 1,
  "zones": [
    {
      "zone_no": 1,
      "slave_id": 1,
      "channels": ["AIR_TEMP", "AIR_HUM", "WATER_RAIL"]
    },
    {
      "zone_no": 2,
      "slave_id": 2,
      "channels": ["AIR_TEMP", "AIR_HUM", "WINDOWS_POS_A", "CURTAIN_POS"]
    }
  ]
}
```

