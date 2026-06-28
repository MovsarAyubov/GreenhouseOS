package hub

import (
	"context"
	"errors"
	"sort"
	"strings"

	"greenhouse-hub/internal/slavemap"
)

var ErrSlaveNotFound = errors.New("slave not found")

type SlaveSetpointCatalog struct {
	SlaveID          uint8                `json:"slave_id"`
	DeviceType       uint16               `json:"device_type"`
	ModbusMapVersion uint16               `json:"modbus_map_version"`
	MapName          string               `json:"map_name"`
	CapabilityMask   uint32               `json:"capability_mask"`
	Groups           []SetpointGroup       `json:"groups"`
}

type SetpointGroup struct {
	Key       string         `json:"key"`
	Label     string         `json:"label"`
	Supported bool           `json:"supported"`
	Items     []SetpointItem `json:"items"`
}

type SetpointItem struct {
	Key   string              `json:"key"`
	Label string              `json:"label"`
	Reg   uint16              `json:"reg"`
	Type  string              `json:"type,omitempty"`
	Scale *float64            `json:"scale,omitempty"`
	Unit  string              `json:"unit,omitempty"`
	Width uint16              `json:"width"`
	Enum  []SetpointEnumValue `json:"enum,omitempty"`
}

type SetpointEnumValue struct {
	Value uint16 `json:"value"`
	Label string `json:"label"`
}

type setpointGroupDef struct {
	key          string
	label        string
	prefixes     []string
	exactKeys    []string
	capabilityOK func(uint32) bool
}

var setpointGroupDefs = []setpointGroupDef{
	{
		key:       "water",
		label:     "Water",
		prefixes:  []string{"water_"},
		capabilityOK: func(mask uint32) bool {
			return hasAnyCapability(mask, 3, 4, 5, 6)
		},
	},
	{
		key:          "windows",
		label:        "Windows",
		prefixes:     []string{"windows_", "window_", "rll400_", "actuator_"},
		capabilityOK: func(mask uint32) bool { return hasAnyCapability(mask, 7, 8) },
	},
	{
		key:          "heating",
		label:        "Heating",
		prefixes:     []string{"heating_"},
		capabilityOK: func(mask uint32) bool { return hasCapability(mask, 10) },
	},
	{
		key:          "curtain",
		label:        "Curtain",
		prefixes:     []string{"curtain_"},
		capabilityOK: func(mask uint32) bool { return hasCapability(mask, 9) },
	},
	{
		key:          "co2",
		label:        "CO2",
		prefixes:     []string{"co2_"},
		capabilityOK: func(mask uint32) bool { return hasCapability(mask, 2) },
	},
	{
		key:          "side_curtain",
		label:        "Side curtain",
		prefixes:     []string{"side_curtain_"},
		capabilityOK: func(mask uint32) bool { return hasCapability(mask, 9) },
	},
	{
		key:          "circulation",
		label:        "Circulation",
		prefixes:     []string{"circ_"},
		capabilityOK: func(mask uint32) bool { return true },
	},
	{
		key:          "light",
		label:        "Light",
		prefixes:     []string{"light_"},
		exactKeys:    []string{"light_current_dli_jcm2"},
		capabilityOK: func(mask uint32) bool { return hasCapability(mask, 11) },
	},
	{
		key:          "weather",
		label:        "Weather",
		prefixes:     []string{"weather_"},
		capabilityOK: func(mask uint32) bool { return hasCapability(mask, 12) },
	},
}

var hiddenSetpointKeys = map[string]bool{
	"command_setpoint_payload": true,
	"command_token":           true,
	"applied_token":           true,
	"command_result":          true,
	"windows_settings":        true,
	"co2_fault_reset_token":   true,
	"weather_token":           true,
	"weather_applied_token":   true,
	"weather_result":          true,
}

var setpointLabelOverrides = map[string]string{
	"water_rail_setpoint":              "Rail water setpoint",
	"water_grow_setpoint":              "Grow pipe water setpoint",
	"water_upper_setpoint":             "Upper heat water setpoint",
	"water_undertray_setpoint":         "Undertray water setpoint",
	"windows_ctrl_mode":                "Windows control mode",
	"windows_force_safe_cmd":           "Force windows safe command",
	"windows_auto_algo_mode":           "Windows auto algorithm",
	"windows_weather_stale_policy":     "Weather stale policy",
	"heating_ctrl_mode":                "Heating control mode",
	"curtain_ctrl_mode":                "Curtain control mode",
	"co2_ctrl_mode":                    "CO2 control mode",
	"co2_manual_outputs":               "CO2 manual outputs",
	"side_curtain_ctrl_mode":           "Side curtain control mode",
	"circ_ctrl_mode":                   "Circulation control mode",
	"light_current_dli_jcm2":           "Current DLI",
	"weather_out_temp":                 "Outside temperature",
	"weather_out_humidity":             "Outside humidity",
	"weather_wind_speed":               "Wind speed",
	"weather_wind_dir":                 "Wind direction",
	"weather_rain_flag":                "Rain flag",
	"weather_solar_rad":                "Solar radiation",
	"weather_baro_press":               "Barometric pressure",
	"weather_dew_point":                "Dew point",
	"weather_status_bits":              "Weather status bits",
	"weather_age_s":                    "Weather age",
}

var enumByKey = map[string][]SetpointEnumValue{
	"windows_ctrl_mode": {
		{Value: 0, Label: "Auto"},
		{Value: 1, Label: "Manual"},
	},
	"windows_auto_algo_mode": {
		{Value: 0, Label: "Temperature"},
		{Value: 1, Label: "Humidity"},
	},
	"windows_rain_mode": {
		{Value: 0, Label: "Off"},
		{Value: 1, Label: "Windward"},
	},
	"windows_weather_stale_policy": {
		{Value: 0, Label: "Close safe"},
		{Value: 1, Label: "Ignore"},
	},
	"heating_ctrl_mode": modeAutoOffManual(),
	"curtain_ctrl_mode": {
		{Value: 0, Label: "Auto"},
		{Value: 1, Label: "Manual"},
		{Value: 2, Label: "Off"},
	},
	"co2_ctrl_mode": modeAutoOffManual(),
	"side_curtain_ctrl_mode": {
		{Value: 0, Label: "Night auto"},
		{Value: 1, Label: "Off"},
		{Value: 2, Label: "Manual"},
	},
	"side_curtain_manual_cmd": {
		{Value: 0, Label: "Stop"},
		{Value: 1, Label: "Open"},
		{Value: 2, Label: "Close"},
	},
	"circ_ctrl_mode": modeAutoOffManual(),
}

func (s *Service) SetpointsForSlave(ctx context.Context, slaveID uint8, includeUnsupported bool) (SlaveSetpointCatalog, error) {
	if _, ok := s.moduleBySlave[slaveID]; !ok {
		return SlaveSetpointCatalog{}, ErrSlaveNotFound
	}

	m, identity, err := s.mapForSlave(ctx, slaveID)
	if err != nil {
		return SlaveSetpointCatalog{}, err
	}

	groups := make([]SetpointGroup, 0, len(setpointGroupDefs))
	for _, def := range setpointGroupDefs {
		supported := def.capabilityOK(identity.CapabilityMask)
		if !supported && !includeUnsupported {
			continue
		}

		items := setpointItemsForGroup(m, def)
		if len(items) == 0 {
			continue
		}
		groups = append(groups, SetpointGroup{
			Key:       def.key,
			Label:     def.label,
			Supported: supported,
			Items:     items,
		})
	}

	return SlaveSetpointCatalog{
		SlaveID:          slaveID,
		DeviceType:       identity.DeviceType,
		ModbusMapVersion: identity.ModbusMapVersion,
		MapName:          m.Name,
		CapabilityMask:   identity.CapabilityMask,
		Groups:           groups,
	}, nil
}

func setpointItemsForGroup(m slavemap.Map, def setpointGroupDef) []SetpointItem {
	registers := writableMapRegisters(m)
	items := make([]SetpointItem, 0)
	for _, reg := range registers {
		if !belongsToGroup(reg.Key, def) || isOperatorHiddenSetpoint(reg.Key) {
			continue
		}
		start, _ := reg.Bounds()
		if !m.IsWritable(start, reg.Width()) {
			continue
		}
		items = append(items, SetpointItem{
			Key:   reg.Key,
			Label: labelForSetpoint(reg.Key),
			Reg:   start,
			Type:  reg.Type,
			Scale: reg.Scale,
			Unit:  reg.Unit,
			Width: reg.Width(),
			Enum:  enumByKey[reg.Key],
		})
	}
	sort.Slice(items, func(i, j int) bool { return items[i].Reg < items[j].Reg })
	return items
}

func writableMapRegisters(m slavemap.Map) []slavemap.Register {
	out := make([]slavemap.Register, 0, len(m.SetpointsCommands)+len(m.SchedulesProfiles)+len(m.SharedExternalData))
	for _, regs := range [][]slavemap.Register{m.SetpointsCommands, m.SchedulesProfiles, m.SharedExternalData} {
		for _, reg := range regs {
			if reg.IsWritable() {
				out = append(out, reg)
			}
		}
	}
	return out
}

func belongsToGroup(key string, def setpointGroupDef) bool {
	for _, exact := range def.exactKeys {
		if key == exact {
			return true
		}
	}
	for _, prefix := range def.prefixes {
		if strings.HasPrefix(key, prefix) {
			return true
		}
	}
	return false
}

func isOperatorHiddenSetpoint(key string) bool {
	if hiddenSetpointKeys[key] {
		return true
	}
	if strings.HasPrefix(key, "rtc_") {
		return true
	}
	if strings.HasSuffix(key, "_fault_reset_token") {
		return true
	}
	return false
}

func labelForSetpoint(key string) string {
	if label, ok := setpointLabelOverrides[key]; ok {
		return label
	}
	parts := strings.Split(key, "_")
	for i, part := range parts {
		switch part {
		case "co2":
			parts[i] = "CO2"
		case "dli":
			parts[i] = "DLI"
		case "hhmm":
			parts[i] = "HHMM"
		case "ppm":
			parts[i] = "ppm"
		case "wm2":
			parts[i] = "W/m2"
		default:
			if part != "" {
				parts[i] = strings.ToUpper(part[:1]) + part[1:]
			}
		}
	}
	return strings.Join(parts, " ")
}

func hasAnyCapability(mask uint32, bits ...uint8) bool {
	for _, bit := range bits {
		if hasCapability(mask, bit) {
			return true
		}
	}
	return false
}

func hasCapability(mask uint32, bit uint8) bool {
	return mask&(uint32(1)<<bit) != 0
}

func modeAutoOffManual() []SetpointEnumValue {
	return []SetpointEnumValue{
		{Value: 0, Label: "Auto"},
		{Value: 1, Label: "Off"},
		{Value: 2, Label: "Manual"},
	}
}
