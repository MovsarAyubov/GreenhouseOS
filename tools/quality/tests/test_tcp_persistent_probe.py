import argparse
import sys
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from tools.quality import tcp_persistent_probe as probe  # noqa: E402


def _diag_regs() -> list[int]:
    regs = [0] * probe.DIAG_QTY_DEFAULT
    regs[20] = 0
    regs[21] = 1
    regs[22] = 0
    regs[23] = 2
    regs[24] = 0
    regs[25] = 3
    regs[26] = 0
    regs[27] = 4
    regs[28] = 0
    regs[29] = 5
    regs[30] = 0xFFFF
    regs[31] = 0xFF9C
    return regs


def _trace_regs() -> list[int]:
    regs = [0] * probe.TRACE_QTY_DEFAULT
    regs[0] = 1
    regs[1] = 7
    regs[2] = 4
    regs[3] = 0
    regs[4] = 11
    regs[5] = 0
    regs[6] = 0
    regs[7] = 6
    regs[8] = 3
    regs[9] = 1264
    regs[10] = 32
    regs[11] = 0
    regs[12] = 123
    regs[13] = 0
    regs[14] = 0x1234
    regs[15] = 0xFFFF
    regs[16] = 0xFFFE
    regs[17] = 0
    regs[18] = 0
    regs[19] = 0
    regs[20] = 0
    return regs


class _FakeClient:
    instances = []

    def __init__(self, *_args, **_kwargs) -> None:
        self._tx_id = 1
        self.connect_calls = 0
        self.close_calls = 0
        self.target_reads = 0
        self.fail_first_target = False
        _FakeClient.instances.append(self)

    def connect(self) -> None:
        self.connect_calls += 1

    def close(self) -> None:
        self.close_calls += 1

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def read_holding_registers(self, address: int, qty: int):
        if address == probe.DIAG_BASE_DEFAULT:
            return _diag_regs()[:qty]
        if address == probe.TRACE_BASE_DEFAULT:
            return _trace_regs()[:qty]
        self.target_reads += 1
        tx_id = self._tx_id
        self._tx_id = 1 if tx_id >= 0xFFFF else tx_id + 1
        if self.fail_first_target and self.target_reads == 1:
            raise TimeoutError("forced timeout")
        return [0] * qty


class TcpPersistentProbeTests(unittest.TestCase):
    def _args(self, **overrides) -> argparse.Namespace:
        base = {
            "host": "127.0.0.1",
            "port": 502,
            "unit_id": 1,
            "timeout_s": 0.05,
            "cycles": 1,
            "reads": [probe.ReadSpec(address=1264, qty=32)],
            "gap_ms": 0,
            "reconnect_per_request": False,
            "reconnect_downtime_s": 0.0,
            "continue_after_timeout": False,
            "diag_base": probe.DIAG_BASE_DEFAULT,
            "diag_qty": probe.DIAG_QTY_DEFAULT,
            "trace_base": probe.TRACE_BASE_DEFAULT,
            "trace_qty": probe.TRACE_QTY_DEFAULT,
            "skip_diag_before": False,
            "skip_diag_after_timeout": False,
            "skip_trace_after_timeout": False,
        }
        base.update(overrides)
        return argparse.Namespace(**base)

    def tearDown(self) -> None:
        _FakeClient.instances.clear()

    def test_run_probe_success_returns_zero(self) -> None:
        args = self._args()
        with mock.patch.object(probe, "ModbusTcpClient", _FakeClient):
            with mock.patch("builtins.print"):
                rc = probe.run_probe(args)
        self.assertEqual(0, rc)
        self.assertEqual(1, len(_FakeClient.instances))
        self.assertEqual(1, _FakeClient.instances[0].target_reads)

    def test_run_probe_timeout_reads_diag_and_trace(self) -> None:
        args = self._args()

        class _TimeoutClient(_FakeClient):
            def __init__(self, *args, **kwargs) -> None:
                super().__init__(*args, **kwargs)
                self.fail_first_target = True

        with mock.patch.object(probe, "ModbusTcpClient", _TimeoutClient):
            with mock.patch("builtins.print"):
                rc = probe.run_probe(args)

        self.assertEqual(2, rc)
        self.assertEqual(1, len(_TimeoutClient.instances))
        self.assertGreaterEqual(_TimeoutClient.instances[0].connect_calls, 2)

    def test_parse_args_rejects_bad_reads(self) -> None:
        with self.assertRaises(SystemExit):
            probe.parse_args(["--host", "127.0.0.1", "--reads", "bad"])


if __name__ == "__main__":
    unittest.main()
