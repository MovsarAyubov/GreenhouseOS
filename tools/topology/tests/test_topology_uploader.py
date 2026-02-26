import json
import tempfile
import unittest
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from tools.topology import topology_uploader as uploader  # noqa: E402


class _FakeClient:
    def __init__(self, reject_token: int | None = None) -> None:
        self._registers = {}
        self._pending_commit_token = None
        self._commit_poll_count = 0
        self._reject_token = reject_token
        self.write_log = []

    def write_single_register(self, address: int, value: int) -> None:
        self._registers[address] = value & 0xFFFF
        self.write_log.append(("w1", address, value & 0xFFFF))
        if address == uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_SUBMIT_TOKEN:
            self._on_submit(value & 0xFFFF)

    def write_multiple_registers(self, address: int, values) -> None:
        for idx, value in enumerate(values):
            self._registers[address + idx] = value & 0xFFFF
        self.write_log.append(("wm", address, list(values)))

    def read_holding_registers(self, address: int, qty: int):
        if qty <= 0:
            raise AssertionError("qty must be > 0")

        if address == uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_RESULT_CODE and qty == 2:
            token = self._registers.get(uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_RESULT_TOKEN, 0)
            code = self._registers.get(uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_RESULT_CODE, 0)
            if self._pending_commit_token == token:
                self._commit_poll_count += 1
                if self._commit_poll_count >= 2:
                    code = uploader.CFG_RESULT_APPLIED
                    self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_RESULT_CODE] = code
                    self._pending_commit_token = None
                    generation_hi = self._registers.get(
                        uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_REQ_GEN_HI, 0
                    )
                    generation_lo = self._registers.get(
                        uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_REQ_GEN_LO, 0
                    )
                    size_hi = self._registers.get(
                        uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_REQ_TOTAL_SIZE_HI, 0
                    )
                    size_lo = self._registers.get(
                        uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_REQ_TOTAL_SIZE_LO, 0
                    )
                    self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_ACTIVE_FLAGS] = 1
                    self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_ACTIVE_GEN_HI] = generation_hi
                    self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_ACTIVE_GEN_LO] = generation_lo
                    self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_ACTIVE_SIZE_HI] = size_hi
                    self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_ACTIVE_SIZE_LO] = size_lo
            return [
                self._registers.get(address, 0),
                self._registers.get(address + 1, 0),
            ]

        return [self._registers.get(address + idx, 0) for idx in range(qty)]

    def _on_submit(self, token: int) -> None:
        flags = self._registers.get(uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_REQ_FLAGS, 0)
        self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_RESULT_TOKEN] = token
        if self._reject_token is not None and token == self._reject_token:
            self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_RESULT_CODE] = 22
            self._pending_commit_token = None
            return

        self._registers[uploader.GH_MB_TOPO_BASE + uploader.TOPO_OFF_RESULT_CODE] = uploader.CFG_RESULT_QUEUED
        if (flags & uploader.TOPOLOGY_UPLOAD_FLAG_COMMIT) != 0:
            self._pending_commit_token = token
            self._commit_poll_count = 0
        else:
            self._pending_commit_token = None


class TopologyUploaderTests(unittest.TestCase):
    def test_load_chunks_file_validation(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "chunks.json"
            path.write_text(
                json.dumps(
                    {
                        "chunks": [
                            {
                                "submit_token": 1,
                                "chunk_index": 0,
                                "chunk_words": 2,
                                "flags": 0,
                                "total_size": 4,
                                "chunk_crc32": 0,
                                "generation": 7,
                                "chunk_data_words": [0x1122, 0x3344],
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )
            chunks = uploader.load_chunks_file(path)
            self.assertEqual(1, len(chunks))
            self.assertEqual(2, chunks[0]["chunk_words"])

    def test_upload_chunks_success(self) -> None:
        chunks = [
            {
                "submit_token": 100,
                "chunk_index": 0,
                "chunk_words": 2,
                "flags": 0,
                "total_size": 6,
                "chunk_crc32": 0xAAAA0001,
                "generation": 11,
                "chunk_data_words": [0x1122, 0x3344],
            },
            {
                "submit_token": 101,
                "chunk_index": 1,
                "chunk_words": 1,
                "flags": uploader.TOPOLOGY_UPLOAD_FLAG_COMMIT,
                "total_size": 6,
                "chunk_crc32": 0xBBBB0002,
                "generation": 11,
                "chunk_data_words": [0x5566],
            },
        ]
        client = _FakeClient()
        summary = uploader.upload_chunks(
            client=client,
            chunks=chunks,
            settings=uploader.UploadSettings(poll_interval_s=0.001, chunk_timeout_s=0.1, commit_timeout_s=0.2),
        )

        self.assertEqual(2, summary["chunks_uploaded"])
        self.assertEqual(1, summary["active_flags"])
        self.assertEqual(11, summary["active_generation"])
        self.assertEqual(6, summary["active_size"])

    def test_upload_chunks_reject(self) -> None:
        chunks = [
            {
                "submit_token": 200,
                "chunk_index": 0,
                "chunk_words": 1,
                "flags": uploader.TOPOLOGY_UPLOAD_FLAG_COMMIT,
                "total_size": 2,
                "chunk_crc32": 0x12345678,
                "generation": 3,
                "chunk_data_words": [0xABCD],
            }
        ]
        client = _FakeClient(reject_token=200)
        with self.assertRaises(uploader.UploaderError):
            uploader.upload_chunks(
                client=client,
                chunks=chunks,
                settings=uploader.UploadSettings(poll_interval_s=0.001, chunk_timeout_s=0.05, commit_timeout_s=0.05),
            )


if __name__ == "__main__":
    unittest.main()
