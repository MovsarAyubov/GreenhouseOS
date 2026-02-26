import struct
import unittest
import zlib
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from tools.topology import topology_packer as packer  # noqa: E402


def _minimal_valid_config() -> dict:
    return {
        "ver_minor": 0,
        "generation": 42,
        "topology_id": 1001,
        "created_unix_s": 1700000000,
        "flags": 0,
        "modules": [
            {
                "module_id": 1,
                "module_type": 1,
                "bus_type": 1,
                "bus_index": 0,
                "slave_id": 1,
                "zone_id": 1,
                "req_first": 0,
                "req_count": 1,
                "cmd_first": 0,
                "cmd_count": 1,
                "offline_reprobe_ms": 30000,
                "heartbeat_timeout_ms": 5000,
                "capability_mask": 0,
                "user_param0": 0,
                "user_param1": 0,
            }
        ],
        "requests": [
            {
                "req_id": 10,
                "module_id": 1,
                "fc": 3,
                "priority": 0,
                "start_reg": 0,
                "reg_count": 8,
                "period_ms": 5000,
                "timeout_ms": 300,
                "retries": 2,
                "backoff_ms": 20,
                "point_first": 0,
                "point_count": 1,
                "flags": 0,
            }
        ],
        "points": [
            {
                "point_id": 100,
                "module_id": 1,
                "req_id": 10,
                "reg_offset": 0,
                "point_type": 1,
                "scale_pow10": 0,
                "bit_index": 0,
                "quality_policy": 0,
                "publish_index": 0,
                "stale_timeout_s": 30,
                "alarm_low": -100,
                "alarm_high": 100,
            }
        ],
        "commands": [
            {
                "cmd_id": 50,
                "module_id": 1,
                "fc": 6,
                "retries": 2,
                "start_reg": 20,
                "max_reg_count": 1,
                "timeout_ms": 300,
                "ack_point_id": 100,
                "flags": 0,
            }
        ],
        "policies": [
            {
                "module_id": 1,
                "on_timeout": 1,
                "on_crc_error": 1,
                "on_link_loss": 1,
                "max_consecutive_fail": 3,
                "recover_good_cycles": 2,
                "safe_profile_id": 0,
            }
        ],
    }


class TopologyPackerTests(unittest.TestCase):
    def test_build_topology_blob_header_crc_and_offsets(self) -> None:
        cfg = _minimal_valid_config()
        blob = packer.build_topology_blob(cfg)

        self.assertEqual(0, len(blob) % 2)
        self.assertLessEqual(len(blob), packer.TOPOLOGY_MAX_BLOB_SIZE)

        header = struct.unpack(packer.HEADER_FMT, blob[: packer.HEADER_SIZE])
        self.assertEqual(packer.GH_TOPOLOGY_V2_MAGIC, header[0])
        self.assertEqual(packer.GH_TOPOLOGY_V2_VERSION_MAJOR, header[1])
        self.assertEqual(cfg["generation"], header[4])
        self.assertEqual(cfg["topology_id"], header[5])
        self.assertEqual(cfg["created_unix_s"], header[6])
        self.assertEqual(1, header[8])   # module_count
        self.assertEqual(1, header[9])   # req_count
        self.assertEqual(1, header[10])  # point_count
        self.assertEqual(1, header[11])  # cmd_count
        self.assertEqual(1, header[12])  # policy_count

        total_size = header[3]
        body_crc = header[19]
        header_crc = header[20]
        self.assertEqual(len(blob), total_size)
        self.assertEqual(packer.HEADER_SIZE, header[14])  # off_modules
        self.assertGreater(header[15], header[14])  # off_requests
        self.assertGreater(header[16], header[15])  # off_points
        self.assertGreater(header[17], header[16])  # off_commands
        self.assertGreater(header[18], header[17])  # off_policies

        body = blob[packer.HEADER_SIZE:total_size]
        self.assertEqual(body_crc, zlib.crc32(body) & 0xFFFFFFFF)

        header_zero = list(header)
        header_zero[20] = 0
        header_zero_blob = struct.pack(packer.HEADER_FMT, *header_zero)
        self.assertEqual(header_crc, zlib.crc32(header_zero_blob) & 0xFFFFFFFF)

    def test_chunk_build_flags_words_crc(self) -> None:
        blob = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
        chunks = packer.build_upload_chunks(blob, generation=9, start_token=100, chunk_words=2)

        self.assertEqual(2, len(chunks))

        first = chunks[0]
        self.assertEqual(100, first["submit_token"])
        self.assertEqual(0, first["chunk_index"])
        self.assertEqual(2, first["chunk_words"])
        self.assertEqual(packer.TOPOLOGY_UPLOAD_FLAG_RESET, first["flags"])
        self.assertEqual([0x1122, 0x3344], first["chunk_data_words"])
        self.assertEqual(zlib.crc32(blob[:4]) & 0xFFFFFFFF, first["chunk_crc32"])

        second = chunks[1]
        self.assertEqual(101, second["submit_token"])
        self.assertEqual(1, second["chunk_index"])
        self.assertEqual(1, second["chunk_words"])
        self.assertEqual(packer.TOPOLOGY_UPLOAD_FLAG_COMMIT, second["flags"])
        self.assertEqual([0x5566], second["chunk_data_words"])
        self.assertEqual(zlib.crc32(blob[4:6]) & 0xFFFFFFFF, second["chunk_crc32"])

    def test_single_chunk_has_reset_and_commit_flags(self) -> None:
        blob = bytes([0xAA, 0x55, 0x12, 0x34])
        chunks = packer.build_upload_chunks(blob, generation=1, start_token=1, chunk_words=8)
        self.assertEqual(1, len(chunks))
        self.assertEqual(
            packer.TOPOLOGY_UPLOAD_FLAG_RESET | packer.TOPOLOGY_UPLOAD_FLAG_COMMIT,
            chunks[0]["flags"],
        )

    def test_reject_unknown_module_reference(self) -> None:
        cfg = _minimal_valid_config()
        cfg["requests"][0]["module_id"] = 2
        with self.assertRaises(packer.TopologyPackError):
            packer.build_topology_blob(cfg)

    def test_reject_bus_type_2_temporarily_unsupported(self) -> None:
        cfg = _minimal_valid_config()
        cfg["modules"][0]["bus_type"] = 2
        with self.assertRaises(packer.TopologyPackError):
            packer.build_topology_blob(cfg)

    def test_reject_unsupported_policy_action(self) -> None:
        cfg = _minimal_valid_config()
        cfg["policies"][0]["on_timeout"] = 3
        with self.assertRaises(packer.TopologyPackError):
            packer.build_topology_blob(cfg)

    def test_reject_modules_over_budget(self) -> None:
        cfg = _minimal_valid_config()
        cfg["modules"] = []
        for idx in range(packer.GH_TOPOLOGY_V2_MAX_MODULES + 1):
            cfg["modules"].append(
                {
                    "module_id": idx + 1,
                    "module_type": 1,
                    "bus_type": 1,
                    "bus_index": 0,
                    "slave_id": 1,
                    "zone_id": 1,
                    "req_first": 0,
                    "req_count": 0,
                    "cmd_first": 0,
                    "cmd_count": 0,
                    "offline_reprobe_ms": 30000,
                    "heartbeat_timeout_ms": 5000,
                    "capability_mask": 0,
                    "user_param0": 0,
                    "user_param1": 0,
                }
            )
        cfg["requests"] = []
        cfg["points"] = []
        cfg["commands"] = []
        cfg["policies"] = []

        with self.assertRaises(packer.TopologyPackError):
            packer.build_topology_blob(cfg)


if __name__ == "__main__":
    unittest.main()
