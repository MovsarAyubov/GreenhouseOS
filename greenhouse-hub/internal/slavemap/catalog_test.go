package slavemap

import (
	"path/filepath"
	"testing"
)

func loadTestZoneMap(t *testing.T) Map {
	t.Helper()

	catalog, err := LoadCatalog(filepath.Join("..", "..", "slave_maps"))
	if err != nil {
		t.Fatalf("LoadCatalog() error = %v", err)
	}
	m, ok := catalog.Find(1, 1)
	if !ok {
		t.Fatal("zone map device_type=1 modbus_map_version=1 not found")
	}
	return m
}

func TestZoneV1ExpandedKeys(t *testing.T) {
	m := loadTestZoneMap(t)

	tests := []struct {
		key    string
		reg    uint16
		access string
	}{
		{"water_rail_setpoint", 1020, "rw"},
		{"windows_ctrl_mode", 1030, "rw"},
		{"windows_status_bits", 1046, "r"},
		{"heating_air_setpoint", 1101, "rw"},
		{"heating_status_bits", 1112, "r"},
		{"curtain_ctrl_mode", 1130, "rw"},
		{"curtain_target", 700, "r"},
		{"air_temp_target", 1160, "rw"},
		{"co2_measured_ppm", 1170, "r"},
		{"co2_external_alarm", 1194, "r"},
		{"co2_ctrl_mode", 1172, "rw"},
		{"side_curtain_manual_target", 1231, "rw"},
		{"side_curtain_target", 1236, "r"},
		{"circ_ctrl_mode", 1240, "rw"},
		{"circ_status_bits", 1263, "r"},
	}

	for _, tt := range tests {
		t.Run(tt.key, func(t *testing.T) {
			reg, ok := m.RegisterByKey(tt.key)
			if !ok {
				t.Fatalf("RegisterByKey(%q) not found", tt.key)
			}
			start, end := reg.Bounds()
			if start != tt.reg || end != tt.reg {
				t.Fatalf("Bounds() = %d..%d, want %d..%d", start, end, tt.reg, tt.reg)
			}
			if reg.Access != tt.access {
				t.Fatalf("Access = %q, want %q", reg.Access, tt.access)
			}
		})
	}
}

func TestZoneV1WritePolicy(t *testing.T) {
	m := loadTestZoneMap(t)

	writable := []uint16{1020, 1030, 1101, 1130, 1160, 1172, 1202, 1231, 1240, 1300}
	for _, address := range writable {
		if !m.IsWritable(address, 1) {
			t.Fatalf("IsWritable(%d, 1) = false, want true", address)
		}
	}

	readOnly := []uint16{700, 1046, 1112, 1170, 1171, 1194, 1195, 1236, 1263, 1304, 1310, 1611}
	for _, address := range readOnly {
		if m.IsWritable(address, 1) {
			t.Fatalf("IsWritable(%d, 1) = true, want false", address)
		}
	}
}

func TestZoneV1LegacyKeysAreAbsent(t *testing.T) {
	m := loadTestZoneMap(t)

	legacyKeys := []string{
		"windows_wind_limit",
		"windows_temp_step_max_index",
		"windows_hum_step_max_index",
		"ctrl_crc_lo",
		"ctrl_crc_hi",
	}
	for _, key := range legacyKeys {
		if _, ok := m.RegisterByKey(key); ok {
			t.Fatalf("legacy key %q should not be present", key)
		}
	}
}

func TestZoneV1NoAddressOverlaps(t *testing.T) {
	m := loadTestZoneMap(t)

	seen := map[uint16]string{}
	for _, reg := range m.allRegisters() {
		start, end := reg.Bounds()
		for address := start; address <= end; address++ {
			if prev, ok := seen[address]; ok {
				t.Fatalf("address %d is used by both %q and %q", address, prev, reg.Key)
			}
			seen[address] = reg.Key
			if address == ^uint16(0) {
				break
			}
		}
	}
}
