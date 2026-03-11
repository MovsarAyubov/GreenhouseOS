import argparse
import sys
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from tools.quality import tcp_integration_probe as probe  # noqa: E402


class _FakeClient:
    def __init__(self, *_args, **_kwargs) -> None:
        self.regs: dict[int, int] = {}
        self.pending_trigger: int | None = None
        self.cmd_base = 1240
        self.dir_base = probe.DIR_BASE_DEFAULT
        self.topo_base = probe.TOPO_BASE_DEFAULT
        self._seed_directory()

    def _seed_directory(self) -> None:
        self.regs[self.dir_base + probe.DIR_OFF_MAP_VERSION] = 4
        self.regs[self.dir_base + probe.DIR_OFF_MAP_FLAGS] = 0x0003
        self.regs[self.dir_base + probe.DIR_OFF_CMD_BASE] = self.cmd_base
        self.regs[self.dir_base + probe.DIR_OFF_CMD_BLOCK_SIZE] = 24
        self.regs[self.cmd_base + probe.CMD_OFF_RESULT] = 0
        self.regs[self.cmd_base + probe.CMD_OFF_LAST_APPLIED_TRIGGER] = 0
        self.regs[self.topo_base + probe.TOPO_OFF_RESULT_CODE] = 0
        self.regs[self.topo_base + probe.TOPO_OFF_RESULT_TOKEN] = 0

    def connect(self) -> None:
        return None

    def close(self) -> None:
        return None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def write_single_register(self, address: int, value: int) -> None:
        self.regs[address] = value & 0xFFFF
        if address == self.cmd_base + probe.CMD_OFF_TRIGGER:
            self._on_trigger(value & 0xFFFF)
        elif address == self.topo_base + probe.TOPO_OFF_SUBMIT_TOKEN:
            self._on_topo_submit(value & 0xFFFF)

    def write_multiple_registers(self, address: int, values) -> None:
        for idx, value in enumerate(values):
            self.regs[address + idx] = value & 0xFFFF

    def read_holding_registers(self, address: int, qty: int):
        if address == self.cmd_base + probe.CMD_OFF_LAST_APPLIED_TRIGGER and qty == 3:
            if self.pending_trigger is not None:
                self.regs[self.cmd_base + probe.CMD_OFF_LAST_APPLIED_TRIGGER] = self.pending_trigger
                self.regs[self.cmd_base + probe.CMD_OFF_RESULT] = probe.DCMD_RESULT_APPLIED
                self.regs[self.cmd_base + probe.CMD_OFF_IO_ERR] = 0
                self.pending_trigger = None
        return [self.regs.get(address + idx, 0) for idx in range(qty)]

    def _on_trigger(self, trigger: int) -> None:
        if self.pending_trigger is not None:
            self.regs[self.cmd_base + probe.CMD_OFF_RESULT] = probe.DCMD_RESULT_REJECT_BUSY
            self.regs[self.cmd_base + probe.CMD_OFF_IO_ERR] = 0
            return
        self.pending_trigger = trigger
        self.regs[self.cmd_base + probe.CMD_OFF_RESULT] = 1
        self.regs[self.cmd_base + probe.CMD_OFF_IO_ERR] = 0

    def _on_topo_submit(self, token: int) -> None:
        chunk_words = self.regs.get(self.topo_base + probe.TOPO_OFF_REQ_CHUNK_WORDS, 0)
        if chunk_words > probe.TOPOLOGY_UPLOAD_CHUNK_WORDS:
            self.regs[self.topo_base + probe.TOPO_OFF_RESULT_CODE] = probe.CFG_RESULT_REJECT_TOPOLOGY_BOUNDS
            self.regs[self.topo_base + probe.TOPO_OFF_RESULT_TOKEN] = token
        else:
            self.regs[self.topo_base + probe.TOPO_OFF_RESULT_CODE] = 1
            self.regs[self.topo_base + probe.TOPO_OFF_RESULT_TOKEN] = token


class TcpIntegrationProbeTests(unittest.TestCase):
    def _args(self, **overrides) -> argparse.Namespace:
        base = {
            "host": "127.0.0.1",
            "port": 502,
            "unit_id": 1,
            "timeout_s": 0.1,
            "dir_base": probe.DIR_BASE_DEFAULT,
            "expected_map_version": 4,
            "points_base": 0,
            "points_qty": 8,
            "diag_base": probe.DIAG_BASE_DEFAULT,
            "diag_qty": 8,
            "run_command": False,
            "cmd_slave_id": 1,
            "cmd_module_id": 101,
            "cmd_profile_id": 5001,
            "cmd_payload": [1, 2, 3],
            "cmd_trigger": 100,
            "accept_command_results": [2],
            "command_timeout_s": 0.2,
            "poll_interval_s": 0.001,
            "run_busy_probe": False,
            "busy_trigger_a": 200,
            "busy_trigger_b": 201,
            "run_invalid_topology_probe": False,
            "topo_base": probe.TOPO_BASE_DEFAULT,
            "topo_probe_token": 300,
        }
        base.update(overrides)
        return argparse.Namespace(**base)

    def test_run_probe_smoke(self) -> None:
        args = self._args()
        with mock.patch.object(probe, "ModbusTcpClient", _FakeClient):
            rc = probe.run_probe(args)
        self.assertEqual(0, rc)

    def test_run_probe_with_command_and_busy_and_topology_fault(self) -> None:
        args = self._args(run_command=True, run_busy_probe=True, run_invalid_topology_probe=True)
        with mock.patch.object(probe, "ModbusTcpClient", _FakeClient):
            rc = probe.run_probe(args)
        self.assertEqual(0, rc)

    def test_parse_args_rejects_bad_points_qty(self) -> None:
        with self.assertRaises(SystemExit):
            probe.parse_args(["--host", "127.0.0.1", "--points-qty", "0"])


if __name__ == "__main__":
    unittest.main()
