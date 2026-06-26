package slavemap

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
)

type Catalog struct {
	maps map[Key]Map
}

type Key struct {
	DeviceType       uint16
	ModbusMapVersion uint16
}

type Map struct {
	Schema                  string        `json:"schema"`
	Name                    string        `json:"name"`
	DeviceType              uint16        `json:"device_type"`
	DeviceTypeName          string        `json:"device_type_name"`
	ModbusMapVersion        uint16        `json:"modbus_map_version"`
	Addressing              string        `json:"addressing"`
	DeprecatedAliasesSupport bool         `json:"deprecated_aliases_supported"`
	Ranges                  []Range       `json:"ranges"`
	Common                  []Register    `json:"common"`
	Capabilities            []Capability  `json:"capabilities"`
	Telemetry               []Register    `json:"telemetry"`
	SetpointsCommands       []Register    `json:"setpoints_commands"`
	SchedulesProfiles       []Register    `json:"schedules_profiles"`
	SharedExternalData      []Register    `json:"shared_external_data"`
	AdvancedDiagnostics     []Register    `json:"advanced_diagnostics"`
	WritePolicy             WritePolicy   `json:"write_policy"`
	byKey                   map[string]Register
	byReg                   map[uint16]Register
}

type Range struct {
	From    uint16 `json:"from"`
	To      uint16 `json:"to"`
	Purpose string `json:"purpose,omitempty"`
}

type Capability struct {
	Bit uint8  `json:"bit"`
	Key string `json:"key"`
}

type Register struct {
	Reg           uint16   `json:"reg,omitempty"`
	From          uint16   `json:"from,omitempty"`
	To            uint16   `json:"to,omitempty"`
	Key           string   `json:"key"`
	Type          string   `json:"type,omitempty"`
	Access        string   `json:"access"`
	Scale         *float64 `json:"scale,omitempty"`
	Unit          string   `json:"unit,omitempty"`
	CapabilityBit *uint8   `json:"capability_bit,omitempty"`
	Reserved      bool     `json:"reserved,omitempty"`
	Note          string   `json:"note,omitempty"`
	Format        string   `json:"format,omitempty"`
	Default       *uint16  `json:"default,omitempty"`
	Examples      []uint16 `json:"examples,omitempty"`
}

type WritePolicy struct {
	WritableRanges []Range `json:"writable_ranges"`
	ReadOnlyRanges []Range `json:"read_only_ranges"`
}

func LoadCatalog(dir string) (*Catalog, error) {
	pattern := filepath.Join(dir, "*.json")
	paths, err := filepath.Glob(pattern)
	if err != nil {
		return nil, err
	}
	if len(paths) == 0 {
		return nil, fmt.Errorf("no slave map JSON files found in %s", dir)
	}

	catalog := &Catalog{maps: make(map[Key]Map)}
	for _, path := range paths {
		m, err := loadMap(path)
		if err != nil {
			return nil, err
		}
		key := Key{DeviceType: m.DeviceType, ModbusMapVersion: m.ModbusMapVersion}
		if _, exists := catalog.maps[key]; exists {
			return nil, fmt.Errorf("duplicate slave map for device_type=%d modbus_map_version=%d", key.DeviceType, key.ModbusMapVersion)
		}
		catalog.maps[key] = m
	}
	return catalog, nil
}

func MustEmptyCatalog() *Catalog {
	return &Catalog{maps: make(map[Key]Map)}
}

func (c *Catalog) Find(deviceType, modbusMapVersion uint16) (Map, bool) {
	if c == nil {
		return Map{}, false
	}
	m, ok := c.maps[Key{DeviceType: deviceType, ModbusMapVersion: modbusMapVersion}]
	return m, ok
}

func (m Map) RegisterByKey(key string) (Register, bool) {
	reg, ok := m.byKey[key]
	return reg, ok
}

func (m Map) RegisterByAddress(address uint16) (Register, bool) {
	reg, ok := m.byReg[address]
	return reg, ok
}

func (m Map) PollRegisters() []Register {
	return m.Telemetry
}

func (m Map) IsWritable(start uint16, count uint16) bool {
	if count == 0 {
		return false
	}
	end := start + count - 1
	if end < start {
		return false
	}
	for _, ro := range m.WritePolicy.ReadOnlyRanges {
		if rangesOverlap(start, end, ro.From, ro.To) {
			return false
		}
	}
	for _, wr := range m.WritePolicy.WritableRanges {
		if start >= wr.From && end <= wr.To {
			return true
		}
	}
	return false
}

func (c *Catalog) List() []Map {
	if c == nil {
		return nil
	}
	out := make([]Map, 0, len(c.maps))
	for _, m := range c.maps {
		out = append(out, m)
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].DeviceType == out[j].DeviceType {
			return out[i].ModbusMapVersion < out[j].ModbusMapVersion
		}
		return out[i].DeviceType < out[j].DeviceType
	})
	return out
}

func loadMap(path string) (Map, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return Map{}, err
	}
	var m Map
	if err := json.Unmarshal(data, &m); err != nil {
		return Map{}, fmt.Errorf("%s: %w", path, err)
	}
	if m.DeviceType == 0 {
		return Map{}, fmt.Errorf("%s: device_type is required", path)
	}
	if m.ModbusMapVersion == 0 {
		return Map{}, fmt.Errorf("%s: modbus_map_version is required", path)
	}
	if m.Name == "" {
		return Map{}, fmt.Errorf("%s: name is required", path)
	}
	m.index()
	return m, nil
}

func (m *Map) index() {
	m.byKey = make(map[string]Register)
	m.byReg = make(map[uint16]Register)
	for _, reg := range m.allRegisters() {
		if reg.Key != "" {
			m.byKey[reg.Key] = reg
		}
		start, end := reg.Bounds()
		for address := start; address <= end; address++ {
			m.byReg[address] = reg
			if address == ^uint16(0) {
				break
			}
		}
	}
}

func (m Map) allRegisters() []Register {
	out := make([]Register, 0,
		len(m.Common)+len(m.Telemetry)+len(m.SetpointsCommands)+len(m.SchedulesProfiles)+
			len(m.SharedExternalData)+len(m.AdvancedDiagnostics),
	)
	out = append(out, m.Common...)
	out = append(out, m.Telemetry...)
	out = append(out, m.SetpointsCommands...)
	out = append(out, m.SchedulesProfiles...)
	out = append(out, m.SharedExternalData...)
	out = append(out, m.AdvancedDiagnostics...)
	return out
}

func (r Register) Bounds() (uint16, uint16) {
	if r.From != 0 || r.To != 0 {
		if r.To < r.From {
			return r.From, r.From
		}
		return r.From, r.To
	}
	return r.Reg, r.Reg
}

func (r Register) Width() uint16 {
	start, end := r.Bounds()
	return end - start + 1
}

func (r Register) IsWritable() bool {
	return r.Access == "rw" || r.Access == "w"
}

func rangesOverlap(aStart, aEnd, bStart, bEnd uint16) bool {
	return aStart <= bEnd && bStart <= aEnd
}
