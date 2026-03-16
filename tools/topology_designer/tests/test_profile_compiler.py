import unittest
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from tools.topology import topology_packer  # noqa: E402
from tools.topology_designer import profile_compiler  # noqa: E402


def _minimal_profile() -> dict:
    return {
        "schema": "greenhouse_profile_v1",
        "ver_minor": 0,
        "generation": 1,
        "topology_id": 11,
        "created_unix_s": 1772064000,
        "flags": 0,
        "site_no": 5,
        "greenhouse_no": 1,
        "zones": [
            {
                "zone_no": 1,
                "slave_id": 1,
                "channels": ["AIR_TEMP", "AIR_HUM", "WATER_RAIL"],
            }
        ],
    }


class ProfileCompilerTests(unittest.TestCase):
    def test_compile_minimal_profile(self) -> None:
        topology = profile_compiler.compile_profile(_minimal_profile())

        self.assertEqual(1, len(topology["modules"]))
        self.assertEqual(1, len(topology["requests"]))
        self.assertEqual(3, len(topology["points"]))
        self.assertEqual(0, len(topology["commands"]))
        self.assertEqual(1, len(topology["policies"]))

        module = topology["modules"][0]
        request = topology["requests"][0]
        points = topology["points"]

        self.assertEqual(101, module["module_id"])
        self.assertEqual(1, module["bus_type"])
        self.assertEqual(1, module["slave_id"])
        self.assertEqual(5, module["capability_mask"])
        self.assertEqual((5 << 24) | (1 << 16) | 1, module["user_param0"])

        self.assertEqual(1010, request["req_id"])
        self.assertEqual(15, request["reg_count"])
        self.assertEqual(3, request["point_count"])

        self.assertEqual([0, 1, 2], [item["reg_offset"] for item in points])
        self.assertEqual([0, 1, 2], [item["publish_index"] for item in points])

    def test_compile_sorts_zones_and_assigns_indexes(self) -> None:
        profile = _minimal_profile()
        profile["zones"] = [
            {
                "zone_no": 2,
                "slave_id": 2,
                "channels": ["AIR_HUM", "WINDOWS_POS_A"],
            },
            {
                "zone_no": 1,
                "slave_id": 1,
                "channels": ["AIR_TEMP"],
            },
        ]

        topology = profile_compiler.compile_profile(profile)
        self.assertEqual([101, 102], [item["module_id"] for item in topology["modules"]])
        self.assertEqual([0, 1], [item["req_first"] for item in topology["modules"]])
        self.assertEqual(0, topology["requests"][0]["point_first"])
        self.assertEqual(1, topology["requests"][1]["point_first"])
        self.assertEqual([15, 15], [item["reg_count"] for item in topology["requests"]])
        self.assertEqual([0, 10, 15], [item["publish_index"] for item in topology["points"]])

    def test_reject_duplicate_bus_slave_mapping(self) -> None:
        profile = _minimal_profile()
        profile["zones"] = [
            {"zone_no": 1, "slave_id": 1, "channels": ["AIR_TEMP"]},
            {"zone_no": 2, "slave_id": 1, "channels": ["AIR_HUM"]},
        ]
        with self.assertRaises(profile_compiler.ProfileError):
            profile_compiler.compile_profile(profile)

    def test_reject_poll_budget_overflow(self) -> None:
        profile = _minimal_profile()
        profile["zones"][0]["poll_period_ms"] = 100
        with self.assertRaises(profile_compiler.ProfileError):
            profile_compiler.compile_profile(profile)

    def test_compiled_topology_is_packer_compatible(self) -> None:
        topology = profile_compiler.compile_profile(_minimal_profile())
        blob = topology_packer.build_topology_blob(topology)
        self.assertGreater(len(blob), 0)
        self.assertEqual(0, len(blob) % 2)


if __name__ == "__main__":
    unittest.main()
