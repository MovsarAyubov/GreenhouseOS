# Master Protocol (STM32F407)

## Transport
- TCP server on port `5000` (integration point in `NetAdapter_*` in firmware)
- Single active client policy: replace old client with new (to be implemented in adapter layer)
- `NetAdapter` has ready `LwIP netconn` implementation behind `GH_USE_LWIP_NETCONN`

## Frame
Header (`proto_header_t`, little-endian):
- `magic: uint16 = 0xA55A`
- `proto_ver: uint8 = 1`
- `msg_type: uint8`
- `seq: uint32`
- `len: uint16`
- `crc32: uint32` (CRC32 payload)

## Message Types
- `1 HELLO` (PC->Master): `last_event_id_seen:uint32`
- `2 HELLO_ACK` (Master->PC): `build_id[16], uptime_ms, active_config_version, last_event_id`
- `3 HEARTBEAT` (both)
- `4 STATUS_REQ` (PC->Master)
- `5 STATUS_RESP` (Master->PC): `status_payload_t`
- `6 SNAPSHOT` (Master->PC): `snapshot_payload_t`
- `7 EVENT` (Master->PC): `event_payload_t`
- `8 EVENT_ACK` (PC->Master): `event_id:uint32`
- `9 GET_CONFIG_REQ` (PC->Master)
- `10 GET_CONFIG_RESP` (Master->PC): raw config payload (`128 bytes`)
- `11 SETPOINTS_PUT` (PC->Master): `version:uint32 + payload_crc:uint32 + payload[128]`
- `12 SETPOINTS_VALIDATE_ACK` (Master->PC): `result:uint8`
- `13 SETPOINTS_APPLY_REQ` (PC->Master)
- `14 SETPOINTS_APPLY_ACK` (Master->PC): `result:uint8 + active_version:uint32`
- `15 GET_BLOCK_LAYOUT_REQ` (PC->Master): empty payload
- `16 BLOCK_LAYOUT_RESP` (Master->PC):
  - `channels_per_block:uint8`
  - `item_count:uint8`
  - `reserved:uint16`
  - `items[12]`, each item:
    - `block_no:uint8`
    - `slave_id:uint8`
    - `start_reg:uint16`
    - `sensor_count:uint16`
    - `sensor_base:uint16`

## Resync Sequence
1. PC connects and sends `HELLO(last_event_id_seen)`
2. Master replies `HELLO_ACK`
3. PC sends `STATUS_REQ`
4. Master replies `STATUS_RESP`
5. PC optionally requests `GET_CONFIG_REQ`
6. PC requests `GET_BLOCK_LAYOUT_REQ` and stores current layout map
7. Master sends backlog events `event_id > last_event_id_seen`
