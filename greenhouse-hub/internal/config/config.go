package config

import (
	"encoding/json"
	"fmt"
	"os"
)

const (
	ModuleTypeZone    = 1
	ModuleTypeWeather = 2
	BusTypeRTU1       = 1
	FCReadHolding     = 3
	FCWriteSingle     = 6
	FCWriteMultiple   = 16
)

type Config struct {
	Topology  Topology
	Semantics Semantics
}

type Topology struct {
	VerMinor     uint16    `json:"ver_minor"`
	Generation   uint32    `json:"generation"`
	TopologyID   uint32    `json:"topology_id"`
	CreatedUnixS uint32    `json:"created_unix_s"`
	Flags        uint32    `json:"flags"`
	Modules      []Module  `json:"modules"`
	Requests     []Request `json:"requests"`
	Points       []Point   `json:"points"`
	Commands     []Command `json:"commands"`
	Policies     []Policy  `json:"policies"`
}

type Module struct {
	ModuleID           uint16 `json:"module_id"`
	ModuleType         uint8  `json:"module_type"`
	BusType            uint8  `json:"bus_type"`
	BusIndex           uint8  `json:"bus_index"`
	SlaveID            uint8  `json:"slave_id"`
	ZoneID             uint16 `json:"zone_id"`
	ReqFirst           uint16 `json:"req_first"`
	ReqCount           uint16 `json:"req_count"`
	CmdFirst           uint16 `json:"cmd_first"`
	CmdCount           uint16 `json:"cmd_count"`
	OfflineReprobeMS   uint16 `json:"offline_reprobe_ms"`
	HeartbeatTimeoutMS uint16 `json:"heartbeat_timeout_ms"`
	CapabilityMask     uint32 `json:"capability_mask"`
	UserParam0          uint32 `json:"user_param0"`
	UserParam1          uint32 `json:"user_param1"`
}

type Request struct {
	ReqID      uint16 `json:"req_id"`
	ModuleID   uint16 `json:"module_id"`
	FC         uint8  `json:"fc"`
	Priority   uint8  `json:"priority"`
	StartReg   uint16 `json:"start_reg"`
	RegCount   uint16 `json:"reg_count"`
	PeriodMS   uint16 `json:"period_ms"`
	TimeoutMS  uint16 `json:"timeout_ms"`
	Retries    uint8  `json:"retries"`
	BackoffMS  uint8  `json:"backoff_ms"`
	PointFirst uint16 `json:"point_first"`
	PointCount uint16 `json:"point_count"`
	Flags      uint16 `json:"flags"`
}

type Point struct {
	PointID       uint16 `json:"point_id"`
	ModuleID      uint16 `json:"module_id"`
	ReqID         uint16 `json:"req_id"`
	RegOffset     uint16 `json:"reg_offset"`
	PointType     uint8  `json:"point_type"`
	ScalePow10    int8   `json:"scale_pow10"`
	BitIndex      uint8  `json:"bit_index"`
	QualityPolicy uint8  `json:"quality_policy"`
	PublishIndex  uint16 `json:"publish_index"`
	StaleTimeoutS uint16 `json:"stale_timeout_s"`
	AlarmLow      int16  `json:"alarm_low"`
	AlarmHigh     int16  `json:"alarm_high"`
}

type Command struct {
	CmdID        uint16 `json:"cmd_id"`
	ModuleID     uint16 `json:"module_id"`
	FC           uint8  `json:"fc"`
	Retries      uint8  `json:"retries"`
	StartReg     uint16 `json:"start_reg"`
	MaxRegCount  uint16 `json:"max_reg_count"`
	PayloadOff   uint16 `json:"payload_offset"`
	TimeoutMS    uint16 `json:"timeout_ms"`
	AckPointID   uint16 `json:"ack_point_id"`
	Flags        uint16 `json:"flags"`
}

type Policy struct {
	ModuleID           uint16 `json:"module_id"`
	OnTimeout          uint8  `json:"on_timeout"`
	OnCRCError         uint8  `json:"on_crc_error"`
	OnLinkLoss         uint8  `json:"on_link_loss"`
	MaxConsecutiveFail uint16 `json:"max_consecutive_fail"`
	RecoverGoodCycles  uint16 `json:"recover_good_cycles"`
	SafeProfileID      uint16 `json:"safe_profile_id"`
}

type Semantics struct {
	Schema            string                     `json:"schema"`
	TopologyID        uint32                     `json:"topology_id"`
	TopologyGeneration uint32                    `json:"topology_generation"`
	TopologyVerMajor  uint16                     `json:"topology_ver_major"`
	TopologyVerMinor  uint16                     `json:"topology_ver_minor"`
	Modules           map[string]SemanticModule  `json:"modules"`
	Points            map[string]SemanticPoint   `json:"points"`
	Commands          map[string]json.RawMessage `json:"commands"`
}

type SemanticModule struct {
	ModuleID   uint16 `json:"module_id"`
	ModuleType uint8  `json:"module_type"`
	SlaveID    uint8  `json:"slave_id"`
	ZoneID     uint16 `json:"zone_id"`
	Title      string `json:"title"`
}

type SemanticPoint struct {
	ModuleID     uint16 `json:"module_id"`
	PointID      uint16 `json:"point_id"`
	PublishIndex uint16 `json:"publish_index"`
	ReqID        uint16 `json:"req_id"`
	Label        string `json:"label"`
	Unit         string `json:"unit"`
	ScalePow10   int8   `json:"scale_pow10"`
}

func Load(topologyPath, semanticsPath string) (*Config, error) {
	var topology Topology
	if err := readJSON(topologyPath, &topology); err != nil {
		return nil, fmt.Errorf("topology: %w", err)
	}
	if len(topology.Modules) == 0 {
		return nil, fmt.Errorf("topology has no modules")
	}

	var semantics Semantics
	if err := readJSON(semanticsPath, &semantics); err != nil {
		return nil, fmt.Errorf("semantics: %w", err)
	}
	if semantics.TopologyID != 0 && semantics.TopologyID != topology.TopologyID {
		return nil, fmt.Errorf("semantics topology_id=%d does not match topology_id=%d", semantics.TopologyID, topology.TopologyID)
	}

	return &Config{Topology: topology, Semantics: semantics}, nil
}

func readJSON(path string, dst any) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(data, dst); err != nil {
		return err
	}
	return nil
}
