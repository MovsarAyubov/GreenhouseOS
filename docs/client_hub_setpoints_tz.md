# TZ for client: sending setpoints through greenhouse-hub HTTP API

Date: `2026-06-28`

## 1. Purpose

Client must send operator setpoints to zone slaves through `greenhouse-hub`.

The client must not calculate slave Modbus register addresses for normal setpoint writes. The primary contract is:

- client reads available setpoints from hub;
- client displays only keys returned by hub;
- client sends setpoint updates by `slave_id + key + value`;
- hub resolves the key to the current slave-map register and writes it to the slave over Modbus RTU.

This prevents register shifts such as writing `windows_ctrl_mode` into `windows_force_safe_cmd`.

## 2. Required Inputs For Client

Client configuration:

- `hub_base_url`, for example `http://192.168.1.50:8080`;
- request timeout: recommended `3..5 s`;
- retry policy for network failures: recommended `1..2` retries with backoff `300..1000 ms`;
- selected `slave_id`, normally discovered from `GET /api/slaves`;
- setpoint key selected from `GET /api/slaves/{slave_id}/setpoints`.

Client must not hardcode register numbers in normal operation. Register numbers from metadata may be shown for diagnostics only.

## 3. Hub API Endpoints Used By Client

Health check:

```text
GET /api/health
```

Expected success:

```json
{"status":"ok"}
```

List known slaves:

```text
GET /api/slaves
```

Read setpoint catalog for one slave:

```text
GET /api/slaves/{slave_id}/setpoints
GET /api/slaves/{slave_id}/setpoints?include_unsupported=1
```

Send setpoint:

```text
POST /api/setpoints
Content-Type: application/json
```

Optional diagnostics:

```text
GET /api/state
GET /api/points
GET /api/slave-maps
POST /api/scan?from=1&to=40
GET /api/scan
```

## 4. Startup Algorithm

On application startup:

1. Call `GET /api/health`.
2. Call `GET /api/slaves`.
3. Keep only slaves where:
   - `online == true` is preferred for active control;
   - `supported == true` is required for key-based setpoint UI;
   - `device_type == 1` and `modbus_map_version == 1` for zone slave v1.
4. For each selected zone slave call `GET /api/slaves/{slave_id}/setpoints`.
5. Build UI from returned `groups[]` and `items[]`.
6. Do not expose hidden service keys that are not returned by this endpoint.

If no supported slave is available, client must show a connection/configuration error and disable write controls.

## 5. Setpoint Catalog Contract

Response shape:

```json
{
  "slave_id": 1,
  "device_type": 1,
  "modbus_map_version": 1,
  "map_name": "Zone Slave Map v1",
  "capability_mask": 724999,
  "groups": [
    {
      "key": "windows",
      "label": "Windows",
      "supported": true,
      "items": [
        {
          "key": "windows_ctrl_mode",
          "label": "Windows control mode",
          "reg": 1030,
          "type": "u16",
          "width": 1,
          "enum": [
            {"value": 0, "label": "Auto"},
            {"value": 1, "label": "Manual"}
          ]
        }
      ]
    }
  ]
}
```

Client rules:

- `groups[].supported == false` means the slave does not advertise the required capability; controls must be disabled unless operator explicitly enables unsupported diagnostics mode.
- `items[].key` is the only identifier to use for writes.
- `items[].reg` is diagnostic metadata only.
- `items[].scale` tells how to convert human units to raw Modbus value.
- `items[].enum` must be rendered as a select/dropdown or segmented control.
- `items[].width > 1` means the key represents a multi-register block; client should normally avoid partial block editing unless product requirements explicitly define the field layout.

## 6. Value Encoding

`POST /api/setpoints` expects raw Modbus register values, not physical values.

For an item with no `scale`:

```text
raw = operator_value
```

For an item with `scale`:

```text
physical_value = raw * scale
raw = round(physical_value / scale)
```

Examples:

- `water_rail_setpoint`, scale `0.1`: operator enters `22.0 degC`, client sends `220`.
- `windows_ctrl_mode`, enum: Auto sends `0`, Manual sends `1`.

Allowed raw range:

- unsigned fields: `0..65535`;
- signed `s16` fields: `-32768..32767`;
- hub rejects values outside Modbus register range.

## 7. Primary Write Request

Use key-based request for all normal setpoint writes:

```json
{
  "slave_id": 1,
  "key": "windows_ctrl_mode",
  "value": 1
}
```

Success response:

```http
202 Accepted
```

```json
{
  "applied": true,
  "mode": "map",
  "slave_id": 1,
  "key": "windows_ctrl_mode",
  "register": 1030,
  "values": [1]
}
```

Failure response:

```http
400 Bad Request
```

```json
{
  "applied": false,
  "error": "key \"...\" is read-only"
}
```

Client must treat only HTTP `202` with `applied=true` as a successful write.

For diagnostics, client must log the success response. Normal key-based writes must return:

- `mode: "map"`;
- `register` equal to the selected catalog item `reg`;
- `values.length == 1` for single-register controls.

If a normal UI write returns `mode: "legacy"` or sends `values` with a leading placeholder such as `[0, value]`, the client is still using the wrong write path.

## 8. Multi-Register Writes

For multi-register keys, client may send:

```json
{
  "slave_id": 1,
  "key": "some_block_key",
  "values": [10, 20, 30]
}
```

Rules:

- use either `value` or `values`, never both;
- `values.length` must be `1..123`;
- `values.length` must not exceed item `width`;
- hub writes from the key start register and increments by payload index.

For v1 client UI, prefer single-register keys and avoid generic block keys. `windows_settings` is intentionally hidden by the hub catalog because the windows UI must use explicit single-register keys.

## 9. Critical Windows Mapping

Current zone slave v1 map:

| Key | Slave register | Type | Access |
|---|---:|---|---|
| `windows_pos_a_target` | `1005` | `u16` | `rw` |
| `windows_pos_b_target` | `1006` | `u16` | `rw` |
| `curtain_pos_target` | `1007` | `u16` | `rw` |
| `windows_ctrl_mode` | `1030` | `u16` | `rw` |
| `windows_force_safe_cmd` | `1031` | `u16` | `rw` |

Required behavior:

- when operator changes windows mode, client must send key `windows_ctrl_mode`;
- client must not send key `windows_force_safe_cmd` for mode changes;
- client must not build a legacy payload where mode is placed at payload index `1`;
- client must not use register `1031` as an alias for mode.
- when operator changes window A target, client must send key `windows_pos_a_target` with scalar `value`, not a `values` block;
- when operator changes window B target, client must send key `windows_pos_b_target` with scalar `value`.

Correct request:

```json
{
  "slave_id": 1,
  "key": "windows_ctrl_mode",
  "value": 1
}
```

Wrong request:

```json
{
  "slave_id": 1,
  "key": "windows_force_safe_cmd",
  "value": 1
}
```

## 10. Recommended UI Behavior

For each catalog item:

- enum field: render select/segmented control from `enum[]`;
- numeric field with `scale`: render physical units, convert to raw before sending;
- numeric field without `scale`: render integer raw value;
- read-only fields are not returned by `/setpoints`; do not create manual controls for them;
- unsupported group: hidden by default; visible disabled only in diagnostics mode.

After a write:

1. Disable the edited control until request completes.
2. On success, show a short success state.
3. Refresh `GET /api/points` or `GET /api/state` if the UI displays feedback.
4. On failure, keep the previous UI value and show hub error text.

## 11. Reading Feedback

For dashboard values, client should read:

```text
GET /api/points
```

Each point includes:

- `key`;
- `slave_id`;
- `register`;
- `value_raw`;
- `value`;
- `quality`;
- `updated_at`;
- optional `error`.

Client must treat `quality != "ok"` as stale/offline and visually mark the value as unreliable.

For slave status, client should read:

```text
GET /api/slaves
```

Important fields:

- `online`;
- `last_ok_at`;
- `fail_count`;
- `last_error`;
- `capability_mask`;
- `device_type`;
- `modbus_map_version`;
- `supported`.

## 12. Error Handling

Client must handle these hub errors:

| Error condition | Client behavior |
|---|---|
| HTTP/network timeout | show hub unavailable, allow retry |
| HTTP `400` with `error` | show validation/write error from response |
| HTTP `404` on `/setpoints` | slave unknown or map unsupported; disable controls |
| `supported=false` slave | do not send key-based writes |
| stale/offline slave | warn operator before write or disable writes, depending on product mode |

Known validation messages may include:

- `slave_id is required`;
- `use either value or values, not both`;
- `value or values length must be 1..123`;
- `key "..." is not in Zone Slave Map v1`;
- `key "..." is read-only`;
- `register range ... is read-only by write-policy`;
- `value[...] is outside Modbus register range`.

## 13. Legacy Command-Profile Mode

Legacy writes are still accepted for compatibility:

```json
{
  "slave_id": 1,
  "module_id": 101,
  "cmd_profile_id": 5004,
  "values": [1]
}
```

Client must not use this mode for new UI work unless explicitly required.

Legacy windows profile `5004` mapping:

| Payload index | Slave register | Meaning |
|---:|---:|---|
| `values[0]` | `1030` | `windows_ctrl_mode` |
| `values[1]` | `1031` | `windows_force_safe_cmd` |

Therefore, to write only `windows_ctrl_mode` through legacy profile, send exactly:

```json
{
  "slave_id": 1,
  "module_id": 101,
  "cmd_profile_id": 5004,
  "values": [1]
}
```

Do not send `[0, 1]` for mode. That writes `1` to `windows_force_safe_cmd`.

## 14. Acceptance Criteria

Client implementation is accepted when:

- it discovers slaves through `GET /api/slaves`;
- it builds setpoint controls from `GET /api/slaves/{slave_id}/setpoints`;
- it sends normal writes through `POST /api/setpoints` using `slave_id + key + value`;
- changing windows mode sends key `windows_ctrl_mode`, never `windows_force_safe_cmd`;
- value scaling matches catalog metadata;
- failed hub responses are shown to operator and do not silently update UI state;
- unsupported/offline slaves cannot be written without explicit diagnostics override;
- no normal UI path hardcodes register `1030` or `1031`.
