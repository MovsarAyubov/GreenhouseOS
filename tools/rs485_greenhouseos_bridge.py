#!/usr/bin/env python3
"""GreenhouseOS-compatible Modbus TCP bridge for ESP32 Modbus RTU slaves.

Topology:
  greenhouse Flutter -> Modbus TCP -> this bridge -> Modbus RTU/RS485 -> ESP32

The bridge continuously polls slave 1, which keeps block_control2 in REMOTE
mode, and exposes the GreenhouseOS register windows expected by the Flutter
SCADA client.
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import serial


GH_MB_POINT_MAX = 180
GH_MB_POINT_STRIDE = 6
GH_MB_MAP_VERSION = 4
GH_MB_POINTS_BASE = 0
GH_MB_SLAVE_STATUS_BLOCK_SIZE = 8
GH_MB_SLAVE_STATUS_BASE = GH_MB_POINTS_BASE + GH_MB_POINT_MAX * GH_MB_POINT_STRIDE
GH_MB_CMD_PAYLOAD_WORDS = 16
GH_MB_CMD_BLOCK_SIZE = 4 + GH_MB_CMD_PAYLOAD_WORDS + 4
GH_MB_CMD_BASE = GH_MB_SLAVE_STATUS_BASE + 20 * GH_MB_SLAVE_STATUS_BLOCK_SIZE
GH_MB_DIR_BASE = GH_MB_CMD_BASE + GH_MB_CMD_BLOCK_SIZE
GH_MB_DIR_REGS = 32
GH_MB_CFG_BASE = GH_MB_DIR_BASE + GH_MB_DIR_REGS
GH_MB_DIAG_BASE = GH_MB_CFG_BASE + 80
GH_MB_TOPO_BASE = GH_MB_DIAG_BASE + 32
GH_MB_TOPO_REGS = 144
GH_MB_TOTAL_REGS = GH_MB_TOPO_BASE + GH_MB_TOPO_REGS

CMD_OFF_TARGET_SLAVE = 0
CMD_OFF_TARGET_MODULE = 1
CMD_OFF_PROFILE_ID = 2
CMD_OFF_PAYLOAD_LEN = 3
CMD_OFF_PAYLOAD = 4
CMD_OFF_TRIGGER = 20
CMD_OFF_LAST_APPLIED_TRIGGER = 21
CMD_OFF_RESULT = 22
CMD_OFF_IO_ERR = 23

DIR_OFF_MAP_VERSION = 0
DIR_OFF_MAP_FLAGS = 1
DIR_OFF_POINT_COUNT = 4
DIR_OFF_POINT_STRIDE = 5
DIR_OFF_POINTS_BASE = 6
DIR_OFF_SLAVE_STATUS_BASE = 7
DIR_OFF_CMD_BASE = 8
DIR_OFF_MAX_POINTS = 11
DIR_OFF_CMD_BLOCK_SIZE = 12
DIR_OFF_STATUS_BLOCK_SIZE = 13
DIR_OFF_RTC_HOUR = 14
DIR_OFF_RTC_MINUTE = 15
DIR_OFF_RTC_SET_HOUR = 16
DIR_OFF_RTC_SET_MINUTE = 17
DIR_OFF_RTC_SET_TOKEN = 18
DIR_OFF_RTC_SET_APPLIED_TOKEN = 19
DIR_OFF_RTC_SET_RESULT = 20

TOPO_OFF_RESULT_CODE = 1
TOPO_OFF_RESULT_TOKEN = 2
TOPO_OFF_ACTIVE_FLAGS = 3
TOPO_OFF_VERSION_MAJOR = 4
TOPO_OFF_VERSION_MINOR = 5
TOPO_OFF_GENERATION_HI = 6
TOPO_OFF_GENERATION_LO = 7
TOPO_OFF_SIZE_HI = 8
TOPO_OFF_SIZE_LO = 9

DCMD_RESULT_APPLIED = 2
DCMD_RESULT_REJECT_TOPOLOGY = 11
DCMD_RESULT_TRANSPORT_FAIL = 15

RTC_RESULT_APPLIED = 2
RTC_RESULT_FAILED = 4

ESP_REG_RTC_SET_BASE = 140
ESP_REG_RTC_RESULT_BASE = 143
ESP_REG_LIGHT_SCHEDULE_BASE = 110


class ModbusError(Exception):
    pass


def u16(value: int) -> int:
    return value & 0xFFFF


def signed16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def float_words(value: float) -> tuple[int, int]:
    raw = struct.unpack(">I", struct.pack(">f", float(value)))[0]
    return (raw >> 16) & 0xFFFF, raw & 0xFFFF


def join_u32_hi_lo(value: int) -> tuple[int, int]:
    return (value >> 16) & 0xFFFF, value & 0xFFFF


def modbus_crc(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


@dataclass(frozen=True)
class TopologyModule:
    module_id: int
    slave_id: int


@dataclass(frozen=True)
class TopologyRequest:
    req_id: int
    module_id: int
    fc: int
    start_reg: int
    reg_count: int


@dataclass(frozen=True)
class TopologyPoint:
    module_id: int
    req_id: int
    reg_offset: int
    scale_pow10: int
    publish_index: int


class RtuClient:
    def __init__(self, port: str, baud: int, timeout_s: float) -> None:
        self._serial = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=8,
            parity=serial.PARITY_NONE,
            stopbits=1,
            timeout=timeout_s,
            write_timeout=timeout_s,
        )
        self._lock = threading.Lock()

    def close(self) -> None:
        self._serial.close()

    def read_holding(self, slave_id: int, address: int, count: int) -> list[int]:
        if count < 1 or count > 125:
            raise ModbusError(f"FC3 count out of range: {count}")
        pdu = struct.pack(">BHH", 0x03, address, count)
        rsp = self._transceive(slave_id, pdu, expected_len=5 + count * 2)
        if rsp[1] & 0x80:
            raise ModbusError(f"FC3 exception {rsp[2]}")
        if rsp[1] != 0x03 or rsp[2] != count * 2:
            raise ModbusError("FC3 malformed response")
        return [
            struct.unpack(">H", rsp[3 + i * 2 : 5 + i * 2])[0]
            for i in range(count)
        ]

    def write_multiple(self, slave_id: int, address: int, values: list[int]) -> None:
        if not values or len(values) > 123:
            raise ModbusError(f"FC16 count out of range: {len(values)}")
        payload = bytearray(
            struct.pack(">BHHB", 0x10, address, len(values), len(values) * 2)
        )
        for value in values:
            payload.extend(struct.pack(">H", value & 0xFFFF))
        rsp = self._transceive(slave_id, bytes(payload), expected_len=8)
        if rsp[1] & 0x80:
            raise ModbusError(f"FC16 exception {rsp[2]}")
        if rsp[1] != 0x10:
            raise ModbusError("FC16 malformed response")

    def _transceive(self, slave_id: int, pdu: bytes, expected_len: int) -> bytes:
        frame = bytearray([slave_id & 0xFF])
        frame.extend(pdu)
        crc = modbus_crc(bytes(frame))
        frame.extend(bytes([crc & 0xFF, (crc >> 8) & 0xFF]))
        with self._lock:
            self._serial.reset_input_buffer()
            self._serial.write(frame)
            self._serial.flush()
            rsp = self._serial.read(expected_len)
        if len(rsp) != expected_len:
            raise TimeoutError(f"RTU timeout {len(rsp)}/{expected_len} bytes")
        got_crc = rsp[-2] | (rsp[-1] << 8)
        if modbus_crc(rsp[:-2]) != got_crc:
            raise ModbusError("RTU CRC mismatch")
        if rsp[0] != (slave_id & 0xFF):
            raise ModbusError(f"RTU slave mismatch: {rsp[0]}")
        return rsp


class GreenhouseBridge:
    def __init__(
        self,
        rtu: RtuClient,
        manifest_path: Path,
        poll_s: float,
        heartbeat_slave_id: int,
        heartbeat_s: float,
    ) -> None:
        self.rtu = rtu
        self.manifest_path = manifest_path
        self.poll_s = poll_s
        self.heartbeat_slave_id = heartbeat_slave_id
        self.heartbeat_s = heartbeat_s
        self.lock = threading.RLock()
        self.regs = [0] * GH_MB_TOTAL_REGS
        self.last_poll_ms_by_slave: dict[int, int] = {}
        self.err_timeout_by_slave: dict[int, int] = {}
        self.err_exception_by_slave: dict[int, int] = {}
        self.last_heartbeat_log_ms = 0
        self.last_error_log_ms_by_slave: dict[int, int] = {}
        self.last_cmd_trigger = 0
        self.last_rtc_token = 0
        self.stop_event = threading.Event()

        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.manifest = manifest
        self.modules = {
            int(item["module_id"]): TopologyModule(
                module_id=int(item["module_id"]),
                slave_id=int(item["slave_id"]),
            )
            for item in manifest.get("modules", [])
            if int(item.get("slave_id", 0)) > 0
        }
        self.requests = {
            int(item["req_id"]): TopologyRequest(
                req_id=int(item["req_id"]),
                module_id=int(item["module_id"]),
                fc=int(item["fc"]),
                start_reg=int(item["start_reg"]),
                reg_count=int(item["reg_count"]),
            )
            for item in manifest.get("requests", [])
        }
        self.points = [
            TopologyPoint(
                module_id=int(item["module_id"]),
                req_id=int(item["req_id"]),
                reg_offset=int(item.get("reg_offset", 0)),
                scale_pow10=int(item.get("scale_pow10", 0)),
                publish_index=int(item["publish_index"]),
            )
            for item in manifest.get("points", [])
            if int(item.get("publish_index", -1)) >= 0
        ]
        self.points_by_req: dict[int, list[TopologyPoint]] = {}
        for point in self.points:
            self.points_by_req.setdefault(point.req_id, []).append(point)

        self.point_count = min(
            GH_MB_POINT_MAX,
            max((point.publish_index for point in self.points), default=-1) + 1,
        )
        self._seed_static_registers()

    def _seed_static_registers(self) -> None:
        generation = int(self.manifest.get("generation", 1))
        schema = str(self.manifest.get("schema_version", "2.0"))
        if "schema_version" not in self.manifest:
            schema = (
                f"{int(self.manifest.get('ver_major', 2))}."
                f"{int(self.manifest.get('ver_minor', 0))}"
            )
        major, _, minor = schema.partition(".")
        size = self.manifest_path.stat().st_size
        gen_hi, gen_lo = join_u32_hi_lo(generation)
        size_hi, size_lo = join_u32_hi_lo(size)
        with self.lock:
            d = GH_MB_DIR_BASE
            self.regs[d + DIR_OFF_MAP_VERSION] = GH_MB_MAP_VERSION
            self.regs[d + DIR_OFF_MAP_FLAGS] = 0x0003
            self.regs[d + DIR_OFF_POINT_COUNT] = self.point_count
            self.regs[d + DIR_OFF_POINT_STRIDE] = GH_MB_POINT_STRIDE
            self.regs[d + DIR_OFF_POINTS_BASE] = GH_MB_POINTS_BASE
            self.regs[d + DIR_OFF_SLAVE_STATUS_BASE] = GH_MB_SLAVE_STATUS_BASE
            self.regs[d + DIR_OFF_CMD_BASE] = GH_MB_CMD_BASE
            self.regs[d + DIR_OFF_MAX_POINTS] = GH_MB_POINT_MAX
            self.regs[d + DIR_OFF_CMD_BLOCK_SIZE] = GH_MB_CMD_BLOCK_SIZE
            self.regs[d + DIR_OFF_STATUS_BLOCK_SIZE] = GH_MB_SLAVE_STATUS_BLOCK_SIZE

            t = GH_MB_TOPO_BASE
            self.regs[t + TOPO_OFF_RESULT_CODE] = 2
            self.regs[t + TOPO_OFF_ACTIVE_FLAGS] = 0x0001
            self.regs[t + TOPO_OFF_VERSION_MAJOR] = int(major or 2)
            self.regs[t + TOPO_OFF_VERSION_MINOR] = int(minor or 0)
            self.regs[t + TOPO_OFF_GENERATION_HI] = gen_hi
            self.regs[t + TOPO_OFF_GENERATION_LO] = gen_lo
            self.regs[t + TOPO_OFF_SIZE_HI] = size_hi
            self.regs[t + TOPO_OFF_SIZE_LO] = size_lo

    def start(self) -> None:
        threading.Thread(target=self._poll_loop, name="rtu-poll", daemon=True).start()

    def read_registers(self, start: int, count: int) -> list[int]:
        if start < 0 or count < 1 or start + count > len(self.regs):
            raise ModbusError("illegal address")
        self._update_point_ages()
        with self.lock:
            return list(self.regs[start : start + count])

    def write_registers(self, start: int, values: Iterable[int]) -> None:
        values = [value & 0xFFFF for value in values]
        if start < 0 or start + len(values) > len(self.regs):
            raise ModbusError("illegal address")
        with self.lock:
            for i, value in enumerate(values):
                self.regs[start + i] = value
        self._process_rtc_if_needed()
        self._process_command_if_needed()

    def _poll_loop(self) -> None:
        while not self.stop_event.is_set():
            self._heartbeat_once()
            for req in self.requests.values():
                module = self.modules.get(req.module_id)
                if module is None or req.fc != 3:
                    continue
                try:
                    values = self.rtu.read_holding(
                        module.slave_id,
                        req.start_reg,
                        req.reg_count,
                    )
                    self._publish_request(module, req, values)
                except TimeoutError as exc:
                    self._mark_timeout(module.slave_id, str(exc))
                except Exception as exc:
                    self._mark_exception(module.slave_id, str(exc))
            self.stop_event.wait(self.poll_s)

    def _heartbeat_once(self) -> None:
        if self.heartbeat_slave_id <= 0:
            return
        try:
            self.rtu.read_holding(self.heartbeat_slave_id, 9, 6)
            self._mark_heartbeat_ok(self.heartbeat_slave_id)
        except TimeoutError as exc:
            self._mark_timeout(self.heartbeat_slave_id, f"heartbeat timeout: {exc}")
        except Exception as exc:
            self._mark_exception(self.heartbeat_slave_id, f"heartbeat error: {exc}")

    def _mark_heartbeat_ok(self, slave_id: int) -> None:
        now_ms = int(time.monotonic() * 1000)
        with self.lock:
            self.last_poll_ms_by_slave[slave_id] = now_ms
            self._set_slave_status(slave_id, online=True)
        if now_ms - self.last_heartbeat_log_ms >= int(self.heartbeat_s * 1000):
            self.last_heartbeat_log_ms = now_ms
            print(f"RTU OK: slave {slave_id} heartbeat", flush=True)

    def _publish_request(
        self,
        module: TopologyModule,
        req: TopologyRequest,
        values: list[int],
    ) -> None:
        now_ms = int(time.monotonic() * 1000)
        with self.lock:
            for point in self.points_by_req.get(req.req_id, []):
                if point.reg_offset >= len(values) or point.publish_index >= GH_MB_POINT_MAX:
                    continue
                raw = signed16(values[point.reg_offset])
                value = raw * (10 ** point.scale_pow10)
                hi, lo = float_words(value)
                base = GH_MB_POINTS_BASE + point.publish_index * GH_MB_POINT_STRIDE
                self.regs[base + 0] = hi
                self.regs[base + 1] = lo
                self.regs[base + 2] = 0
                self.regs[base + 3] = 0
                self.regs[base + 4] = module.module_id
                self.regs[base + 5] = 0x0001
            self._set_slave_status(module.slave_id, online=True)
            self.last_poll_ms_by_slave[module.slave_id] = now_ms

    def _set_slave_status(self, slave_id: int, online: bool) -> None:
        if slave_id < 1 or slave_id > 20:
            return
        base = GH_MB_SLAVE_STATUS_BASE + (slave_id - 1) * GH_MB_SLAVE_STATUS_BLOCK_SIZE
        self.regs[base + 0] = 0x0001 if online else 0x0002
        self.regs[base + 1] = 0
        self.regs[base + 2] = self.err_timeout_by_slave.get(slave_id, 0)
        self.regs[base + 4] = self.err_exception_by_slave.get(slave_id, 0)
        self.regs[base + 5] = u16(self.regs[base + 5] + 1)
        self.regs[base + 6] = 0x01FF
        self.regs[base + 7] = 0

    def _mark_timeout(self, slave_id: int, detail: str | None = None) -> None:
        with self.lock:
            self.err_timeout_by_slave[slave_id] = (
                self.err_timeout_by_slave.get(slave_id, 0) + 1
            )
            self._set_slave_status(slave_id, online=False)
        self._log_rtu_error(slave_id, detail or "timeout")

    def _mark_exception(self, slave_id: int, detail: str | None = None) -> None:
        with self.lock:
            self.err_exception_by_slave[slave_id] = (
                self.err_exception_by_slave.get(slave_id, 0) + 1
            )
            self._set_slave_status(slave_id, online=False)
        self._log_rtu_error(slave_id, detail or "exception")

    def _log_rtu_error(self, slave_id: int, detail: str) -> None:
        now_ms = int(time.monotonic() * 1000)
        last_ms = self.last_error_log_ms_by_slave.get(slave_id, 0)
        if now_ms - last_ms < 5000:
            return
        self.last_error_log_ms_by_slave[slave_id] = now_ms
        print(f"RTU ERROR: slave {slave_id}: {detail}", flush=True)

    def _update_point_ages(self) -> None:
        now_ms = int(time.monotonic() * 1000)
        with self.lock:
            for slave_id, last_ms in self.last_poll_ms_by_slave.items():
                age_sec = min(0xFFFF, max(0, (now_ms - last_ms) // 1000))
                status_base = (
                    GH_MB_SLAVE_STATUS_BASE
                    + (slave_id - 1) * GH_MB_SLAVE_STATUS_BLOCK_SIZE
                )
                self.regs[status_base + 1] = age_sec
            for point in self.points:
                module = self.modules.get(point.module_id)
                if module is None:
                    continue
                last_ms = self.last_poll_ms_by_slave.get(module.slave_id)
                age_sec = 0xFFFF if last_ms is None else min(0xFFFF, (now_ms - last_ms) // 1000)
                base = GH_MB_POINTS_BASE + point.publish_index * GH_MB_POINT_STRIDE
                self.regs[base + 3] = age_sec

    def _process_rtc_if_needed(self) -> None:
        with self.lock:
            token = self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_SET_TOKEN]
            hour = self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_SET_HOUR]
            minute = self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_SET_MINUTE]
        if token == 0 or token == self.last_rtc_token:
            return
        self.last_rtc_token = token
        try:
            self.rtu.write_multiple(1, ESP_REG_RTC_SET_BASE, [hour, minute, token])
            deadline = time.monotonic() + 3.0
            result = RTC_RESULT_FAILED
            while time.monotonic() < deadline:
                regs = self.rtu.read_holding(1, ESP_REG_RTC_RESULT_BASE, 2)
                if regs[0] == token:
                    result = regs[1]
                    break
                time.sleep(0.1)
            with self.lock:
                self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_SET_APPLIED_TOKEN] = token
                self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_SET_RESULT] = result
                if result == RTC_RESULT_APPLIED:
                    self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_HOUR] = hour
                    self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_MINUTE] = minute
        except Exception:
            with self.lock:
                self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_SET_APPLIED_TOKEN] = token
                self.regs[GH_MB_DIR_BASE + DIR_OFF_RTC_SET_RESULT] = RTC_RESULT_FAILED

    def _process_command_if_needed(self) -> None:
        with self.lock:
            trigger = self.regs[GH_MB_CMD_BASE + CMD_OFF_TRIGGER]
            target_slave = self.regs[GH_MB_CMD_BASE + CMD_OFF_TARGET_SLAVE]
            payload_len = self.regs[GH_MB_CMD_BASE + CMD_OFF_PAYLOAD_LEN]
            payload = self.regs[
                GH_MB_CMD_BASE + CMD_OFF_PAYLOAD :
                GH_MB_CMD_BASE + CMD_OFF_PAYLOAD + min(payload_len, GH_MB_CMD_PAYLOAD_WORDS)
            ]
        if trigger == 0 or trigger == self.last_cmd_trigger:
            return
        self.last_cmd_trigger = trigger
        if target_slave < 1:
            self._mark_command(trigger, DCMD_RESULT_REJECT_TOPOLOGY, 0)
            return
        try:
            schedule = self._schedule_payload_to_esp(payload)
            self.rtu.write_multiple(target_slave, ESP_REG_LIGHT_SCHEDULE_BASE, schedule)
            print(
                "Light schedule sent to ESP "
                f"slave={target_slave} regs 110..122: {schedule}",
                flush=True,
            )
            self._mark_command(trigger, DCMD_RESULT_APPLIED, 0)
        except Exception as exc:
            print(f"RTU ERROR: schedule send failed: {exc}", flush=True)
            self._mark_command(trigger, DCMD_RESULT_TRANSPORT_FAIL, 1)

    def _schedule_payload_to_esp(self, payload: list[int]) -> list[int]:
        schedule = [0] * 13
        for index, value in enumerate(payload[:13]):
            schedule[index] = u16(value)
        return schedule

    def _mark_command(self, trigger: int, result: int, io_err: int) -> None:
        with self.lock:
            self.regs[GH_MB_CMD_BASE + CMD_OFF_LAST_APPLIED_TRIGGER] = trigger
            self.regs[GH_MB_CMD_BASE + CMD_OFF_RESULT] = result
            self.regs[GH_MB_CMD_BASE + CMD_OFF_IO_ERR] = io_err


class ModbusTcpServer:
    def __init__(self, bridge: GreenhouseBridge, host: str, port: int) -> None:
        self.bridge = bridge
        self.host = host
        self.port = port

    def serve_forever(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind((self.host, self.port))
            srv.listen()
            print(f"GreenhouseOS RS485 bridge listening on {self.host}:{self.port}", flush=True)
            while True:
                conn, addr = srv.accept()
                threading.Thread(target=self._client, args=(conn, addr), daemon=True).start()

    def _client(self, conn: socket.socket, addr: tuple[str, int]) -> None:
        with conn:
            while True:
                header = self._recv_exact(conn, 7)
                if not header:
                    return
                tx_id, proto_id, length, unit_id = struct.unpack(">HHHB", header)
                payload = self._recv_exact(conn, length - 1)
                if not payload:
                    return
                try:
                    response = self._handle_pdu(payload)
                except ModbusError:
                    response = bytes([payload[0] | 0x80, 0x02])
                except Exception:
                    response = bytes([payload[0] | 0x80, 0x04])
                adu = struct.pack(">HHHB", tx_id, proto_id, len(response) + 1, unit_id) + response
                conn.sendall(adu)

    def _handle_pdu(self, pdu: bytes) -> bytes:
        fc = pdu[0]
        if fc == 0x03:
            start, count = struct.unpack(">HH", pdu[1:5])
            regs = self.bridge.read_registers(start, count)
            out = bytearray([0x03, count * 2])
            for reg in regs:
                out.extend(struct.pack(">H", reg & 0xFFFF))
            return bytes(out)
        if fc == 0x06:
            addr, value = struct.unpack(">HH", pdu[1:5])
            self.bridge.write_registers(addr, [value])
            return pdu[:5]
        if fc == 0x10:
            start, count, byte_count = struct.unpack(">HHB", pdu[1:6])
            if byte_count != count * 2:
                raise ModbusError("bad FC16 byte count")
            values = [
                struct.unpack(">H", pdu[6 + i * 2 : 8 + i * 2])[0]
                for i in range(count)
            ]
            self.bridge.write_registers(start, values)
            return struct.pack(">BHH", 0x10, start, count)
        raise ModbusError(f"unsupported fc {fc}")

    def _recv_exact(self, conn: socket.socket, count: int) -> bytes:
        chunks = bytearray()
        while len(chunks) < count:
            chunk = conn.recv(count - len(chunks))
            if not chunk:
                return b""
            chunks.extend(chunk)
        return bytes(chunks)


def main() -> int:
    parser = argparse.ArgumentParser(description="GreenhouseOS Modbus TCP bridge for ESP32 RS485")
    parser.add_argument("--serial-port", default="COM5")
    parser.add_argument("--baud", type=int, default=19200)
    parser.add_argument("--rtu-timeout-s", type=float, default=0.4)
    parser.add_argument("--tcp-host", default="127.0.0.1")
    parser.add_argument("--tcp-port", type=int, default=1502)
    parser.add_argument("--poll-s", type=float, default=1.0)
    parser.add_argument("--heartbeat-slave-id", type=int, default=1)
    parser.add_argument("--heartbeat-log-s", type=float, default=10.0)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).resolve().parents[1]
        / "topology"
        / "one_zone_one_weather_all_points_schedule_topology.json",
    )
    args, unknown = parser.parse_known_args()
    if unknown:
        print(f"Ignoring extra arguments: {' '.join(unknown)}", flush=True)

    print(
        f"Opening RS485 RTU {args.serial_port} at {args.baud} 8N1, "
        f"heartbeat slave={args.heartbeat_slave_id}",
        flush=True,
    )
    rtu = RtuClient(args.serial_port, args.baud, args.rtu_timeout_s)
    bridge = GreenhouseBridge(
        rtu,
        args.manifest,
        args.poll_s,
        args.heartbeat_slave_id,
        args.heartbeat_log_s,
    )
    print(
        f"Loaded topology {args.manifest} "
        f"({len(bridge.modules)} modules, {len(bridge.requests)} requests)",
        flush=True,
    )
    bridge.start()
    try:
        ModbusTcpServer(bridge, args.tcp_host, args.tcp_port).serve_forever()
    finally:
        bridge.stop_event.set()
        rtu.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
