# Config Payload v1

## Binary layout
- Fixed `128` bytes.
- Interpreted as `32 x float32` setpoints (`little-endian`).

## Validation rules
- Length must be exactly `128`.
- CRC32 must match payload.
- Every float must be finite and in range `[-100.0, 1000.0]`.

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
