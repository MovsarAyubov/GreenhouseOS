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
		{"windows_force_safe_cmd", 1031, "rw"},
		{"windows_runtime_status", 430, "r"},
		{"windows_wind_storm", 1035, "rw"},
		{"windows_wind_recover", 1036, "rw"},
		{"windows_windward_speed_threshold", 1054, "rw"},
		{"heating_ctrl_mode", 1100, "rw"},
		{"heating_air_setpoint", 1101, "rw"},
		{"curtain_ctrl_mode", 1130, "rw"},
		{"curtain_pos_target", 1007, "rw"},
		{"air_temp_target", 1160, "rw"},
		{"co2_runtime_status", 470, "r"},
		{"co2_ctrl_mode", 1172, "rw"},
		{"circ_ctrl_mode", 1210, "rw"},
		{"side_curtain_ctrl_mode", 1230, "rw"},
		{"light_r1_enable", 1300, "rw"},
	}

	for _, tt := range tests {
		t.Run(tt.key, func(t *testing.T) {
			reg, ok := m.RegisterByKey(tt.key)
			if !ok {
				t.Fatalf("RegisterByKey(%q) not found", tt.key)
			}
			start, end := reg.Bounds()
			if start != tt.reg {
				t.Fatalf("Bounds() starts at %d, want %d (end=%d)", start, tt.reg, end)
			}
			if reg.Access != tt.access {
				t.Fatalf("Access = %q, want %q", reg.Access, tt.access)
			}
		})
	}
}

func TestZoneV1WritePolicy(t *testing.T) {
	m := loadTestZoneMap(t)

	writable := []uint16{1000, 1020, 1030, 1031, 1057, 1101, 1130, 1160, 1172, 1210, 1231, 1300, 1613, 1800}
	for _, address := range writable {
		if !m.IsWritable(address, 1) {
			t.Fatalf("IsWritable(%d, 1) = false, want true", address)
		}
	}

	readOnly := []uint16{32, 400, 430, 450, 470, 701, 1016, 1611, 1803}
	for _, address := range readOnly {
		if m.IsWritable(address, 1) {
			t.Fatalf("IsWritable(%d, 1) = true, want false", address)
		}
	}
}

func TestZoneV1AccessMatchesWritePolicy(t *testing.T) {
	m := loadTestZoneMap(t)

	for _, reg := range m.allRegisters() {
		start, end := reg.Bounds()
		for address := start; address <= end; address++ {
			writable := m.IsWritable(address, 1)
			if reg.IsWritable() && !writable {
				t.Fatalf("%q at %d has access=%q but is not writable by policy", reg.Key, address, reg.Access)
			}
			if !reg.IsWritable() && writable {
				t.Fatalf("%q at %d has access=%q but is writable by policy", reg.Key, address, reg.Access)
			}
			if address == ^uint16(0) {
				break
			}
		}
	}
}

func TestZoneV1OpaqueAndReservedKeysAreAbsent(t *testing.T) {
	m := loadTestZoneMap(t)

	absentKeys := []string{
		"windows_settings",
		"heating_settings",
		"curtain_settings",
		"co2_settings_input",
		"circ_settings",
		"side_curtain_settings",
		"windows_reserved_173",
		"windows_reserved_196",
		"light_r1_reserved",
		"light_r2_reserved",
	}
	for _, key := range absentKeys {
		if _, ok := m.RegisterByKey(key); ok {
			t.Fatalf("opaque or reserved key %q should not be present", key)
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
