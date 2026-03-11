import argparse
import sys
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from tools.quality import tcp_soak_test  # noqa: E402


def _make_args(**overrides) -> argparse.Namespace:
    params = {
        "host": "127.0.0.1",
        "port": 502,
        "unit_id": 1,
        "duration_h": 0.000002,
        "poll_s": 0.001,
        "timeout_s": 0.05,
        "points_base": 0,
        "points_qty": 8,
        "diag_base": 1376,
        "diag_qty": 8,
        "diag_every_cycles": 0,
        "reconnect_every_cycles": 0,
        "reconnect_probability": 0.0,
        "reconnect_downtime_s": 0.0,
        "io_retries": 1,
        "upload_interval_s": 1.0,
        "chunks": None,
    }
    params.update(overrides)
    return argparse.Namespace(**params)


def _build_fake_client_class(fail_first_reads: int = 0, fail_all_reads: bool = False):
    class _FakeClient:
        instances = []

        def __init__(self, host: str, port: int, unit_id: int, timeout_s: float) -> None:
            self.host = host
            self.port = port
            self.unit_id = unit_id
            self.timeout_s = timeout_s
            self.connect_calls = 0
            self.close_calls = 0
            self.read_calls = 0
            self.remaining_fail_reads = fail_first_reads
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
            self.read_calls += 1
            if fail_all_reads:
                raise TimeoutError("forced read timeout")
            if self.remaining_fail_reads > 0:
                self.remaining_fail_reads -= 1
                raise TimeoutError("transient read timeout")
            return [0] * qty

    return _FakeClient


class TcpSoakTestTests(unittest.TestCase):
    def test_run_soak_success_returns_zero(self) -> None:
        fake_client = _build_fake_client_class()
        args = _make_args()

        with mock.patch.object(tcp_soak_test, "ModbusTcpClient", fake_client):
            with mock.patch("builtins.print"):
                rc = tcp_soak_test.run_soak(args)

        self.assertEqual(0, rc)
        self.assertEqual(1, len(fake_client.instances))
        self.assertGreaterEqual(fake_client.instances[0].read_calls, 1)

    def test_run_soak_transient_fault_is_recovered(self) -> None:
        fake_client = _build_fake_client_class(fail_first_reads=1)
        args = _make_args()

        with mock.patch.object(tcp_soak_test, "ModbusTcpClient", fake_client):
            with mock.patch("builtins.print"):
                rc = tcp_soak_test.run_soak(args)

        self.assertEqual(0, rc)
        self.assertEqual(1, len(fake_client.instances))
        self.assertGreaterEqual(fake_client.instances[0].read_calls, 2)

    def test_run_soak_persistent_fault_returns_non_zero(self) -> None:
        fake_client = _build_fake_client_class(fail_all_reads=True)
        args = _make_args()

        with mock.patch.object(tcp_soak_test, "ModbusTcpClient", fake_client):
            with mock.patch("builtins.print"):
                rc = tcp_soak_test.run_soak(args)

        self.assertEqual(2, rc)

    def test_parse_args_rejects_zero_duration(self) -> None:
        argv = ["tcp_soak_test.py", "--host", "127.0.0.1", "--duration-h", "0"]
        with mock.patch.object(sys, "argv", argv):
            with self.assertRaises(SystemExit):
                tcp_soak_test.parse_args()

    def test_parse_args_rejects_missing_chunks_file(self) -> None:
        argv = ["tcp_soak_test.py", "--host", "127.0.0.1", "--chunks", "missing_chunks.json"]
        with mock.patch.object(sys, "argv", argv):
            with self.assertRaises(SystemExit):
                tcp_soak_test.parse_args()


if __name__ == "__main__":
    unittest.main()
