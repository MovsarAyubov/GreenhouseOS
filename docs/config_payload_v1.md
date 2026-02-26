# Config Payload v1

## Binary layout
- Fixed `128` bytes.
- Interpreted as `32 x float32` setpoints (`little-endian`).

## Validation rules
- Length must be exactly `128`.
- CRC32 must match payload.
- Every float must be finite and in range `[-100.0, 1000.0]`.
- Version must be strictly greater than current active config version.

## Submit over Modbus TCP
Use global config window at `GH_MB_CFG_BASE=1472` (`42472` with `4xxxx` offset):
- write `REQ_VERSION` (`+10..+11`)
- write `REQ_CRC32` (`+12..+13`)
- write payload words to `+16..+79`
- write non-zero changing `SUBMIT_TOKEN` to `+0`

Payload word packing:
- For each 16-bit register word `W`, bytes are placed as:
  - `payload[2*i] = high_byte(W)`
  - `payload[2*i+1] = low_byte(W)`

## Flash storage
Two-slot A/B in internal flash:
- Slot A: `0x08040000` (`FLASH_SECTOR_6`)
- Slot B: `0x08060000` (`FLASH_SECTOR_7`)

Slot header (`config_slot_header_t`):
- `version:uint32`
- `len:uint32`
- `crc:uint32`
- `seq:uint32`
- `valid_marker:uint32`

`valid_marker=0xA55A5AA5` is written last.
