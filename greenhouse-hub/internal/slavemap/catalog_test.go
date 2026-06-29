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

func loadTestWeatherMap(t *testing.T) Map {
	t.Helper()

	catalog, err := LoadCatalog(filepath.Join("..", "..", "slave_maps"))
	if err != nil {
		t.Fatalf("LoadCatalog() error = %v", err)
	}
	m, ok := catalog.Find(2, 1)
	if !ok {
		t.Fatal("weather map device_type=2 modbus_map_version=1 not found")
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

func TestWeatherV1IdentityBlock(t *testing.T) {
	m := loadTestWeatherMap(t)

	if m.Schema != "greenhouse_slave_map_v1" {
		t.Fatalf("Schema = %q, want greenhouse_slave_map_v1", m.Schema)
	}
	if m.Name != "Weather Station Map v1" {
		t.Fatalf("Name = %q, want Weather Station Map v1", m.Name)
	}
	if m.Addressing != "zero_based_holding_register" {
		t.Fatalf("Addressing = %q, want zero_based_holding_register", m.Addressing)
	}
	if m.DeprecatedAliasesSupport {
		t.Fatal("DeprecatedAliasesSupport = true, want false")
	}

	tests := []struct {
		key string
		reg uint16
	}{
		{"device_type", 0},
		{"firmware_version", 1},
		{"modbus_map_version", 2},
		{"zone_id", 3},
		{"capability_low", 4},
		{"capability_high", 5},
		{"status_bits", 6},
		{"fault_code", 7},
		{"uptime_hi", 8},
		{"uptime_lo", 9},
		{"last_master_age_s", 10},
		{"restart_counter", 11},
		{"config_version", 12},
		{"serial_hi", 13},
		{"serial_lo", 14},
		{"identity_crc", 15},
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
			if reg.Type != "u16" {
				t.Fatalf("Type = %q, want u16", reg.Type)
			}
			if reg.Access != "r" {
				t.Fatalf("Access = %q, want r", reg.Access)
			}
		})
	}
}

func TestWeatherV1Telemetry(t *testing.T) {
	m := loadTestWeatherMap(t)

	tests := []struct {
		key           string
		reg           uint16
		typ           string
		scale         *float64
		unit          string
		capabilityBit uint8
	}{
		{"out_temp", 400, "s16", floatPtr(0.1), "degC", 0},
		{"out_humidity", 401, "u16", floatPtr(0.1), "%RH", 1},
		{"wind_speed", 402, "u16", floatPtr(0.1), "m/s", 2},
		{"wind_dir", 403, "u16", floatPtr(1), "deg", 3},
		{"rain_flag", 404, "u16", nil, "", 4},
		{"solar_rad", 405, "u16", floatPtr(1), "W/m2", 5},
		{"baro_press", 406, "u16", floatPtr(0.1), "hPa", 6},
		{"dew_point", 407, "s16", floatPtr(0.1), "degC", 7},
		{"weather_status_bits", 408, "u16", nil, "", 8},
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
			if reg.Type != tt.typ {
				t.Fatalf("Type = %q, want %q", reg.Type, tt.typ)
			}
			if !sameOptionalFloat(reg.Scale, tt.scale) {
				t.Fatalf("Scale = %v, want %v", reg.Scale, tt.scale)
			}
			if reg.Unit != tt.unit {
				t.Fatalf("Unit = %q, want %q", reg.Unit, tt.unit)
			}
			if reg.Access != "r" {
				t.Fatalf("Access = %q, want r", reg.Access)
			}
			if reg.CapabilityBit == nil || *reg.CapabilityBit != tt.capabilityBit {
				t.Fatalf("CapabilityBit = %v, want %d", reg.CapabilityBit, tt.capabilityBit)
			}
		})
	}
}

func TestWeatherV1Capabilities(t *testing.T) {
	m := loadTestWeatherMap(t)

	want := map[uint8]string{
		0: "outdoor_temperature",
		1: "outdoor_humidity",
		2: "wind_speed",
		3: "wind_direction",
		4: "rain_flag",
		5: "solar_radiation",
		6: "barometric_pressure",
		7: "dew_point",
		8: "weather_status",
	}
	if len(m.Capabilities) != len(want) {
		t.Fatalf("len(Capabilities) = %d, want %d", len(m.Capabilities), len(want))
	}
	for _, capability := range m.Capabilities {
		key, ok := want[capability.Bit]
		if !ok {
			t.Fatalf("unexpected capability bit %d key %q", capability.Bit, capability.Key)
		}
		if capability.Key != key {
			t.Fatalf("capability bit %d key = %q, want %q", capability.Bit, capability.Key, key)
		}
	}
}

func TestWeatherV1ReadOnlyPolicy(t *testing.T) {
	m := loadTestWeatherMap(t)

	if len(m.WritePolicy.WritableRanges) != 0 {
		t.Fatalf("WritableRanges length = %d, want 0", len(m.WritePolicy.WritableRanges))
	}
	wantReadOnly := []Range{
		{From: 0, To: 99},
		{From: 400, To: 999},
		{From: 1000, To: 1999},
	}
	if !sameRanges(m.WritePolicy.ReadOnlyRanges, wantReadOnly) {
		t.Fatalf("ReadOnlyRanges = %#v, want %#v", m.WritePolicy.ReadOnlyRanges, wantReadOnly)
	}
	for _, address := range []uint16{0, 15, 32, 39, 400, 408, 999, 1000, 1999} {
		if m.IsWritable(address, 1) {
			t.Fatalf("IsWritable(%d, 1) = true, want false", address)
		}
	}
	for _, reg := range m.allRegisters() {
		if reg.IsWritable() {
			t.Fatalf("%q has access=%q, want read-only", reg.Key, reg.Access)
		}
	}
}

func TestWeatherV1NoAddressOverlaps(t *testing.T) {
	m := loadTestWeatherMap(t)

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

func floatPtr(value float64) *float64 {
	return &value
}

func sameOptionalFloat(got, want *float64) bool {
	if got == nil || want == nil {
		return got == want
	}
	return *got == *want
}

func sameRanges(got, want []Range) bool {
	if len(got) != len(want) {
		return false
	}
	for i := range got {
		if got[i].From != want[i].From || got[i].To != want[i].To {
			return false
		}
	}
	return true
}
