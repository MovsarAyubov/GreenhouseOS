# Topology Upload Protocol (Modbus TCP, Port 502)

Date: `2026-03-08`.

This document defines the exact client-side write sequence for uploading `topology_config v2` into firmware.

## 1. Register window

- Base: `GH_MB_TOPO_BASE = 1408` (SCADA `42408`).
- Size: `GH_MB_TOPO_REGS = 144`.

Offsets (`base + off`):
- `0` `SUBMIT_TOKEN` (W)
- `1` `RESULT_CODE` (R)
- `2` `RESULT_TOKEN` (R)
- `3` `ACTIVE_FLAGS` (R), bit0=`active`
- `4` `ACTIVE_VER_MAJOR` (R)
- `5` `ACTIVE_VER_MINOR` (R)
- `6..7` `ACTIVE_GENERATION` (R, hi/lo)
- `8..9` `ACTIVE_SIZE_BYTES` (R, hi/lo)
- `10` `REQ_CHUNK_INDEX` (W)
- `11` `REQ_CHUNK_WORDS` (W), max `120`
- `12..13` `REQ_TOTAL_SIZE_BYTES` (W, hi/lo)
- `14..15` `REQ_CHUNK_CRC32` (W, hi/lo)
- `16` `REQ_FLAGS` (W)
- `17..18` `REQ_GENERATION` (W, hi/lo)
- `20..139` `REQ_CHUNK_DATA_WORDS[120]` (W)

Flags (`REQ_FLAGS`):
- bit0 `0x0001`: `COMMIT`
- bit1 `0x0002`: `RESET_STAGING`

## 2. Payload/chunk rules

- Maximum topology blob size: `4096` bytes.
- Maximum chunk payload: `120` words = `240` bytes.
- Each chunk request contains:
  - `chunk_index`
  - `chunk_words`
  - `total_size`
  - `generation`
  - `chunk_crc32`
  - `chunk_data_words[]`
- CRC is calculated over exactly `chunk_words * 2` bytes (network byte order inside each word: high byte first).

Chunk count:
- `required_chunks = ceil(total_size / 240)`.
- Last chunk may be shorter.

## 3. Submit token behavior

- Backend enqueues request only when `SUBMIT_TOKEN` changes to a non-zero value.
- Rewriting the same token does not resubmit.
- `RESULT_TOKEN` mirrors token of last processed request.

## 4. Client upload sequence

1. Split full topology blob into chunks (`<=240` bytes each).
2. Choose a non-zero `token`, increment per request.
3. For chunk 0:
   - write `REQ_CHUNK_INDEX = 0`
   - write `REQ_CHUNK_WORDS`
   - write `REQ_TOTAL_SIZE_BYTES`
   - write `REQ_CHUNK_CRC32`
   - write `REQ_FLAGS = RESET_STAGING` (without `COMMIT`)
   - write `REQ_GENERATION`
   - write `REQ_CHUNK_DATA_WORDS`
   - write `SUBMIT_TOKEN = token`
4. For middle chunks:
   - same fields, `REQ_FLAGS = 0`
   - increment token
   - write `SUBMIT_TOKEN`
5. For final chunk:
   - set `REQ_FLAGS = COMMIT` (optional `RESET_STAGING` must be `0` here in normal flow)
   - increment token
   - write `SUBMIT_TOKEN`
6. Poll `RESULT_CODE` and `RESULT_TOKEN` after each submit.
7. Success criterion for final chunk: `RESULT_CODE == 2 (APPLIED)` and `RESULT_TOKEN == final token`.

## 5. Expected result codes

- `1` `QUEUED`: chunk accepted into pipeline.
- `2` `APPLIED`: full topology validated and activated.
- `13` `REJECT_QUEUE_FULL`: queue/backpressure issue.
- `14` `FLASH_FAIL`: write failure.
- `20` `REJECT_TOPOLOGY_SCHEMA`: invalid order/format/version.
- `21` `REJECT_TOPOLOGY_BOUNDS`: invalid sizes/indexes/chunk_words.
- `22` `REJECT_TOPOLOGY_CRC`: chunk or payload CRC mismatch.
- `23` `REJECT_TOPOLOGY_COLLISION`: overlapping table sections.
- `24` `REJECT_TOPOLOGY_BUDGET`: exceeds firmware limits.

Full code list: `docs/error_codes.md`.

## 6. Recovery behavior

- On reject, previously active topology remains active.
- Client should restart upload from chunk 0 with `RESET_STAGING`.
- Use new `generation` only for new accepted topology candidate.

## 7. Compatibility

- Transport is standard Modbus TCP on port `502`.
- No client changes are needed at TCP framing level.
- Required client change is only register-level upload logic described above.
