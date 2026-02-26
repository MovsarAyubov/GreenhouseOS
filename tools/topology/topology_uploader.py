#!/usr/bin/env python3
"""Upload topology chunks to greenhouseOS via Modbus TCP."""

from __future__ import annotations

import argparse
import json
import socket
import struct
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Sequence

GH_MB_TOPO_BASE = 1392

TOPO_OFF_SUBMIT_TOKEN = 0
TOPO_OFF_RESULT_CODE = 1
TOPO_OFF_RESULT_TOKEN = 2
TOPO_OFF_ACTIVE_FLAGS = 3
TOPO_OFF_ACTIVE_VER_MAJOR = 4
TOPO_OFF_ACTIVE_VER_MINOR = 5
TOPO_OFF_ACTIVE_GEN_HI = 6
TOPO_OFF_ACTIVE_GEN_LO = 7
TOPO_OFF_ACTIVE_SIZE_HI = 8
TOPO_OFF_ACTIVE_SIZE_LO = 9
TOPO_OFF_REQ_CHUNK_INDEX = 10
TOPO_OFF_REQ_CHUNK_WORDS = 11
TOPO_OFF_REQ_TOTAL_SIZE_HI = 12
TOPO_OFF_REQ_TOTAL_SIZE_LO = 13
TOPO_OFF_REQ_CHUNK_CRC_HI = 14
TOPO_OFF_REQ_CHUNK_CRC_LO = 15
TOPO_OFF_REQ_FLAGS = 16
TOPO_OFF_REQ_GEN_HI = 17
TOPO_OFF_REQ_GEN_LO = 18
TOPO_OFF_CHUNK_BASE = 20

TOPOLOGY_UPLOAD_CHUNK_WORDS = 120
TOPOLOGY_UPLOAD_FLAG_COMMIT = 0x0001

CFG_RESULT_IDLE = 0
CFG_RESULT_QUEUED = 1
CFG_RESULT_APPLIED = 2

RESULT_NAMES = {
    0: "IDLE",
    1: "QUEUED",
    2: "APPLIED",
    10: "REJECT_BAD_VERSION",
    11: "REJECT_BAD_CRC",
    12: "REJECT_RANGE",
    13: "REJECT_QUEUE_FULL",
    14: "FLASH_FAIL",
    15: "APPLY_QUEUE_FAIL",
    20: "REJECT_TOPOLOGY_SCHEMA",
    21: "REJECT_TOPOLOGY_BOUNDS",
    22: "REJECT_TOPOLOGY_CRC",
    23: "REJECT_TOPOLOGY_COLLISION",
    24: "REJECT_TOPOLOGY_BUDGET",
}


class UploaderError(RuntimeError):
    """Raised when upload fails."""


class ModbusTcpError(UploaderError):
    """Raised for Modbus TCP protocol errors."""


def _u16(value: Any, name: str) -> int:
    if not isinstance(value, int):
        raise UploaderError(f"{name}: integer expected")
    if value < 0 or value > 0xFFFF:
        raise UploaderError(f"{name}: value {value} out of range [0..65535]")
    return value


def _u32(value: Any, name: str) -> int:
    if not isinstance(value, int):
        raise UploaderError(f"{name}: integer expected")
    if value < 0 or value > 0xFFFFFFFF:
        raise UploaderError(f"{name}: value {value} out of range [0..4294967295]")
    return value


def _split_u32(value: int) -> tuple[int, int]:
    return ((value >> 16) & 0xFFFF, value & 0xFFFF)


def _join_u32(hi: int, lo: int) -> int:
    return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)


@dataclass(frozen=True)
class UploadSettings:
    base_addr: int = GH_MB_TOPO_BASE
    poll_interval_s: float = 0.1
    chunk_timeout_s: float = 3.0
    commit_timeout_s: float = 15.0
    io_retries: int = 5


def _is_transport_error(exc: BaseException) -> bool:
    return isinstance(exc, (TimeoutError, OSError, ModbusTcpError))


def _try_reconnect_client(client: Any) -> None:
    close_fn = getattr(client, "close", None)
    connect_fn = getattr(client, "connect", None)
    if callable(close_fn):
        try:
            close_fn()
        except Exception:  # noqa: BLE001
            pass
    if callable(connect_fn):
        connect_fn()


def _run_io_with_retry(op_desc: str,
                       operation,
                       retries: int,
                       retry_delay_s: float,
                       client: Any | None = None) -> Any:
    last_error: BaseException | None = None
    attempts = max(1, retries + 1)
    for _ in range(attempts):
        try:
            return operation()
        except BaseException as exc:  # noqa: BLE001
            if not _is_transport_error(exc):
                raise
            last_error = exc
            if client is not None:
                try:
                    _try_reconnect_client(client)
                except BaseException as reconnect_exc:  # noqa: BLE001
                    last_error = reconnect_exc
            time.sleep(retry_delay_s)
    raise UploaderError(f"{op_desc}: transport failed after {attempts} attempts: {last_error}")


class ModbusTcpClient:
    def __init__(self, host: str, port: int, unit_id: int, timeout_s: float) -> None:
        self._host = host
        self._port = port
        self._unit_id = unit_id
        self._timeout_s = timeout_s
        self._sock: socket.socket | None = None
        self._tx_id = 1

    def connect(self) -> None:
        self._sock = socket.create_connection((self._host, self._port), timeout=self._timeout_s)
        self._sock.settimeout(self._timeout_s)

    def close(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def __enter__(self) -> "ModbusTcpClient":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def read_holding_registers(self, address: int, qty: int) -> List[int]:
        if qty <= 0 or qty > 125:
            raise ModbusTcpError(f"FC3 qty out of range: {qty}")
        pdu = struct.pack(">BHH", 0x03, address, qty)
        rsp = self._transceive(pdu)
        if rsp[0] == 0x83:
            raise ModbusTcpError(f"FC3 exception code {rsp[1]}")
        if rsp[0] != 0x03:
            raise ModbusTcpError(f"FC3 invalid response function {rsp[0]}")
        byte_count = rsp[1]
        if byte_count != qty * 2:
            raise ModbusTcpError(f"FC3 invalid byte count {byte_count}, expected {qty * 2}")
        regs = []
        for idx in range(qty):
            regs.append(struct.unpack_from(">H", rsp, 2 + (2 * idx))[0])
        return regs

    def write_single_register(self, address: int, value: int) -> None:
        pdu = struct.pack(">BHH", 0x06, address, value & 0xFFFF)
        rsp = self._transceive(pdu)
        if rsp[0] == 0x86:
            raise ModbusTcpError(f"FC6 exception code {rsp[1]}")
        if rsp != pdu:
            raise ModbusTcpError("FC6 response mismatch")

    def write_multiple_registers(self, address: int, values: Sequence[int]) -> None:
        qty = len(values)
        if qty <= 0 or qty > 123:
            raise ModbusTcpError(f"FC16 qty out of range: {qty}")
        payload = bytearray()
        for value in values:
            payload.extend(struct.pack(">H", value & 0xFFFF))
        pdu = struct.pack(">BHHB", 0x10, address, qty, len(payload)) + payload
        rsp = self._transceive(pdu)
        if rsp[0] == 0x90:
            raise ModbusTcpError(f"FC16 exception code {rsp[1]}")
        if len(rsp) != 5 or rsp[0] != 0x10:
            raise ModbusTcpError("FC16 invalid response")
        _, rsp_addr, rsp_qty = struct.unpack(">BHH", rsp)
        if rsp_addr != address or rsp_qty != qty:
            raise ModbusTcpError("FC16 response echo mismatch")

    def _transceive(self, pdu: bytes) -> bytes:
        if self._sock is None:
            raise ModbusTcpError("socket not connected")

        tx_id = self._tx_id
        self._tx_id = 1 if self._tx_id >= 0xFFFF else self._tx_id + 1
        frame = struct.pack(">HHHB", tx_id, 0, len(pdu) + 1, self._unit_id) + pdu

        self._sock.sendall(frame)
        hdr = self._recv_exact(7)
        rx_tx_id, proto, length, unit_id = struct.unpack(">HHHB", hdr)
        if rx_tx_id != tx_id:
            raise ModbusTcpError(f"tx_id mismatch: expected {tx_id}, got {rx_tx_id}")
        if proto != 0:
            raise ModbusTcpError(f"protocol id mismatch: {proto}")
        if unit_id != self._unit_id:
            raise ModbusTcpError(f"unit id mismatch: expected {self._unit_id}, got {unit_id}")
        if length < 2:
            raise ModbusTcpError(f"invalid response length: {length}")
        body = self._recv_exact(length - 1)
        return body

    def _recv_exact(self, size: int) -> bytes:
        assert self._sock is not None
        chunks = bytearray()
        while len(chunks) < size:
            chunk = self._sock.recv(size - len(chunks))
            if not chunk:
                raise ModbusTcpError("socket closed by peer")
            chunks.extend(chunk)
        return bytes(chunks)


def load_chunks_file(path: Path) -> List[Dict[str, Any]]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise UploaderError(f"chunks file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise UploaderError(f"invalid chunks JSON: {exc}") from exc

    if not isinstance(raw, dict):
        raise UploaderError("chunks root must be an object")
    chunks = raw.get("chunks")
    if not isinstance(chunks, list) or not chunks:
        raise UploaderError("chunks must be a non-empty list")
    normalized = [_normalize_chunk(item, idx) for idx, item in enumerate(chunks)]
    return normalized


def _normalize_chunk(item: Any, idx: int) -> Dict[str, Any]:
    if not isinstance(item, dict):
        raise UploaderError(f"chunks[{idx}] must be an object")

    token = _u16(item.get("submit_token"), f"chunks[{idx}].submit_token")
    if token == 0:
        raise UploaderError(f"chunks[{idx}].submit_token must be non-zero")

    chunk_index = _u16(item.get("chunk_index"), f"chunks[{idx}].chunk_index")
    chunk_words = _u16(item.get("chunk_words"), f"chunks[{idx}].chunk_words")
    flags = _u16(item.get("flags"), f"chunks[{idx}].flags")
    total_size = _u32(item.get("total_size"), f"chunks[{idx}].total_size")
    chunk_crc32 = _u32(item.get("chunk_crc32"), f"chunks[{idx}].chunk_crc32")
    generation = _u32(item.get("generation"), f"chunks[{idx}].generation")
    words = item.get("chunk_data_words")
    if not isinstance(words, list):
        raise UploaderError(f"chunks[{idx}].chunk_data_words must be a list")
    word_values = [_u16(value, f"chunks[{idx}].chunk_data_words[{word_idx}]") for word_idx, value in enumerate(words)]

    if chunk_words != len(word_values):
        raise UploaderError(
            f"chunks[{idx}].chunk_words={chunk_words} does not match data length={len(word_values)}"
        )
    if chunk_words > TOPOLOGY_UPLOAD_CHUNK_WORDS:
        raise UploaderError(
            f"chunks[{idx}].chunk_words={chunk_words} exceeds max {TOPOLOGY_UPLOAD_CHUNK_WORDS}"
        )

    return {
        "submit_token": token,
        "chunk_index": chunk_index,
        "chunk_words": chunk_words,
        "flags": flags,
        "total_size": total_size,
        "chunk_crc32": chunk_crc32,
        "generation": generation,
        "chunk_data_words": word_values,
    }


def _wait_for_result(client: Any,
                     token: int,
                     expect_applied: bool,
                     settings: UploadSettings) -> int:
    deadline = time.monotonic() + (settings.commit_timeout_s if expect_applied else settings.chunk_timeout_s)
    last_transport_error: BaseException | None = None
    while time.monotonic() < deadline:
        try:
            result_code, result_token = _run_io_with_retry(
                op_desc=f"read result token={token}",
                operation=lambda: client.read_holding_registers(
                    settings.base_addr + TOPO_OFF_RESULT_CODE, 2
                ),
                retries=settings.io_retries,
                retry_delay_s=settings.poll_interval_s,
                client=client,
            )
        except BaseException as exc:  # noqa: BLE001
            if not _is_transport_error(exc):
                raise
            last_transport_error = exc
            time.sleep(settings.poll_interval_s)
            continue

        if result_token != token:
            time.sleep(settings.poll_interval_s)
            continue

        if expect_applied:
            if result_code == CFG_RESULT_APPLIED:
                return result_code
            if result_code == CFG_RESULT_QUEUED:
                time.sleep(settings.poll_interval_s)
                continue
        else:
            if result_code == CFG_RESULT_QUEUED:
                return result_code
            if result_code == CFG_RESULT_APPLIED:
                return result_code

        raise UploaderError(
            f"token {token}: backend rejected with result={result_code} ({RESULT_NAMES.get(result_code, 'UNKNOWN')})"
        )

    if last_transport_error is not None:
        raise UploaderError(f"token {token}: timeout waiting for backend result (last transport error: {last_transport_error})")
    raise UploaderError(f"token {token}: timeout waiting for backend result")


def upload_chunks(client: Any,
                  chunks: Sequence[Dict[str, Any]],
                  settings: UploadSettings = UploadSettings()) -> Dict[str, int]:
    total = len(chunks)
    if total == 0:
        raise UploaderError("chunks list is empty")

    for idx, chunk in enumerate(chunks):
        token = chunk["submit_token"]
        commit = (chunk["flags"] & TOPOLOGY_UPLOAD_FLAG_COMMIT) != 0

        total_size_hi, total_size_lo = _split_u32(chunk["total_size"])
        crc_hi, crc_lo = _split_u32(chunk["chunk_crc32"])
        gen_hi, gen_lo = _split_u32(chunk["generation"])

        meta_values = [
            chunk["chunk_index"],
            chunk["chunk_words"],
            total_size_hi,
            total_size_lo,
            crc_hi,
            crc_lo,
            chunk["flags"],
            gen_hi,
            gen_lo,
        ]

        print(
            f"[{idx + 1}/{total}] submit token={token} chunk={chunk['chunk_index']} "
            f"words={chunk['chunk_words']} flags=0x{chunk['flags']:04X}"
        )

        _run_io_with_retry(
            op_desc=f"write chunk meta token={token}",
            operation=lambda: client.write_multiple_registers(
                settings.base_addr + TOPO_OFF_REQ_CHUNK_INDEX, meta_values
            ),
            retries=settings.io_retries,
            retry_delay_s=settings.poll_interval_s,
            client=client,
        )
        if chunk["chunk_words"] > 0:
            _run_io_with_retry(
                op_desc=f"write chunk data token={token}",
                operation=lambda: client.write_multiple_registers(
                    settings.base_addr + TOPO_OFF_CHUNK_BASE, chunk["chunk_data_words"]
                ),
                retries=settings.io_retries,
                retry_delay_s=settings.poll_interval_s,
                client=client,
            )
        _run_io_with_retry(
            op_desc=f"write submit token={token}",
            operation=lambda: client.write_single_register(
                settings.base_addr + TOPO_OFF_SUBMIT_TOKEN, token
            ),
            retries=settings.io_retries,
            retry_delay_s=settings.poll_interval_s,
            client=client,
        )

        result_code = _wait_for_result(
            client=client,
            token=token,
            expect_applied=commit,
            settings=settings,
        )
        print(f"  -> result={result_code} ({RESULT_NAMES.get(result_code, 'UNKNOWN')})")

    active = _run_io_with_retry(
        op_desc="read active topology state",
        operation=lambda: client.read_holding_registers(settings.base_addr + TOPO_OFF_ACTIVE_FLAGS, 7),
        retries=settings.io_retries,
        retry_delay_s=settings.poll_interval_s,
        client=client,
    )
    active_flags = active[0]
    active_gen = _join_u32(active[3], active[4])
    active_size = _join_u32(active[5], active[6])
    return {
        "chunks_uploaded": total,
        "active_flags": active_flags,
        "active_generation": active_gen,
        "active_size": active_size,
    }


def _build_cli_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Target server IP/DNS")
    parser.add_argument("--port", type=int, default=502, help="Modbus TCP port")
    parser.add_argument("--unit-id", type=int, default=1, help="Modbus unit id")
    parser.add_argument("--chunks", required=True, type=Path, help="Path to topology_chunks.json")
    parser.add_argument("--base-addr", type=int, default=GH_MB_TOPO_BASE, help="Topology register base")
    parser.add_argument("--timeout-s", type=float, default=2.0, help="Socket timeout")
    parser.add_argument("--poll-interval-ms", type=int, default=100, help="Result poll interval")
    parser.add_argument("--chunk-timeout-ms", type=int, default=3000, help="Timeout for non-commit chunk")
    parser.add_argument("--commit-timeout-ms", type=int, default=15000, help="Timeout for commit chunk")
    parser.add_argument("--io-retries", type=int, default=5, help="Retries per Modbus operation on timeout/network errors")
    return parser


def main() -> int:
    parser = _build_cli_parser()
    args = parser.parse_args()

    try:
        chunks = load_chunks_file(args.chunks)
        settings = UploadSettings(
            base_addr=args.base_addr,
            poll_interval_s=max(args.poll_interval_ms, 1) / 1000.0,
            chunk_timeout_s=max(args.chunk_timeout_ms, 100) / 1000.0,
            commit_timeout_s=max(args.commit_timeout_ms, 100) / 1000.0,
            io_retries=max(args.io_retries, 0),
        )
        with ModbusTcpClient(args.host, args.port, args.unit_id, args.timeout_s) as client:
            summary = upload_chunks(client, chunks, settings)
    except UploaderError as exc:
        print(f"uploader error: {exc}")
        return 1
    except (OSError, socket.error) as exc:
        print(f"uploader error: network failure: {exc}")
        return 1

    print("Upload complete.")
    print(f"Chunks uploaded: {summary['chunks_uploaded']}")
    print(f"Active flags: 0x{summary['active_flags']:04X}")
    print(f"Active generation: {summary['active_generation']}")
    print(f"Active size: {summary['active_size']} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
