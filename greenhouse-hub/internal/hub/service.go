package hub

import (
	"context"
	"errors"
	"fmt"
	"log"
	"math"
	"sort"
	"sync"
	"time"

	"greenhouse-hub/internal/config"
	"greenhouse-hub/internal/modbusrtu"
	"greenhouse-hub/internal/slavemap"
)

const (
	QualityOK      = "ok"
	QualityStale   = "stale"
	QualityOffline = "offline"
)

type Options struct {
	ScanFrom uint8
	ScanTo   uint8
}

type Service struct {
	cfg       *config.Config
	maps      *slavemap.Catalog
	transport modbusrtu.Transport
	options   Options

	ops chan operation

	mu        sync.RWMutex
	points    map[string]PointState
	slaves    map[uint8]SlaveState
	lastScan  ScanResult
	startedAt time.Time

	reqByID      map[uint16]config.Request
	moduleByID   map[uint16]config.Module
	moduleBySlave map[uint8]config.Module
	pointsByReq  map[uint16][]config.Point
	semanticByPI map[uint16]SemanticPointRef
}

type PointState struct {
	Key          string    `json:"key"`
	PointID      uint16    `json:"point_id"`
	ModuleID     uint16    `json:"module_id"`
	SlaveID      uint8     `json:"slave_id"`
	ZoneID       uint16    `json:"zone_id"`
	PublishIndex uint16    `json:"publish_index"`
	Register     uint16    `json:"register"`
	MapName      string    `json:"map_name,omitempty"`
	Label        string    `json:"label"`
	Unit         string    `json:"unit"`
	Type         string    `json:"type,omitempty"`
	ValueRaw     uint32    `json:"value_raw"`
	Value        float64   `json:"value"`
	Quality      string    `json:"quality"`
	UpdatedAt    time.Time `json:"updated_at"`
	Error        string    `json:"error,omitempty"`
}

type SlaveState struct {
	SlaveID       uint8     `json:"slave_id"`
	ModuleID      uint16    `json:"module_id"`
	ModuleType    uint8     `json:"module_type"`
	ZoneID        uint16    `json:"zone_id"`
	Online        bool      `json:"online"`
	LastOKAt      time.Time `json:"last_ok_at,omitempty"`
	LastErrorAt   time.Time `json:"last_error_at,omitempty"`
	FailCount     uint32    `json:"fail_count"`
	LastError     string    `json:"last_error,omitempty"`
	CapabilityMask uint32   `json:"capability_mask"`
	DeviceType     uint16   `json:"device_type,omitempty"`
	ModbusMapVersion uint16 `json:"modbus_map_version,omitempty"`
	MapName        string   `json:"map_name,omitempty"`
	Supported      bool     `json:"supported"`
}

type SemanticPointRef struct {
	Key   string
	Point config.SemanticPoint
}

type Snapshot struct {
	StartedAt  time.Time              `json:"started_at"`
	TopologyID uint32                 `json:"topology_id"`
	Generation uint32                 `json:"generation"`
	Slaves     []SlaveState           `json:"slaves"`
	Points     []PointState           `json:"points"`
	Modules    map[string]config.SemanticModule `json:"modules"`
}

type SetpointRequest struct {
	SlaveID      uint8    `json:"slave_id"`
	ModuleID     uint16   `json:"module_id"`
	CmdProfileID uint16   `json:"cmd_profile_id"`
	Key          string   `json:"key"`
	Value        *int     `json:"value,omitempty"`
	Values       []int    `json:"values"`
}

type SetpointResult struct {
	Applied  bool     `json:"applied"`
	Error    string   `json:"error,omitempty"`
	Mode     string   `json:"mode,omitempty"`
	SlaveID  uint8    `json:"slave_id,omitempty"`
	Key      string   `json:"key,omitempty"`
	Register *uint16  `json:"register,omitempty"`
	Values   []uint16 `json:"values,omitempty"`
}

type DiscoveredDevice struct {
	Address          uint8     `json:"address"`
	Expected         bool      `json:"expected"`
	DeviceType       uint16    `json:"device_type"`
	FirmwareVersion  uint16    `json:"firmware_version"`
	ModbusMapVersion uint16    `json:"modbus_map_version"`
	ZoneID           uint16    `json:"zone_id"`
	CapabilityMask   uint32    `json:"capability_mask"`
	Status           uint16    `json:"status"`
	FaultCode        uint16    `json:"fault_code"`
	MapName          string    `json:"map_name,omitempty"`
	Supported        bool      `json:"supported"`
	SeenAt           time.Time `json:"seen_at"`
}

type ScanResult struct {
	StartedAt time.Time          `json:"started_at"`
	FinishedAt time.Time         `json:"finished_at"`
	From      uint8              `json:"from"`
	To        uint8              `json:"to"`
	Devices   []DiscoveredDevice `json:"devices"`
	Errors    map[uint8]string   `json:"errors,omitempty"`
}

type operation struct {
	run  func(context.Context) error
	done chan error
}

func NewService(cfg *config.Config, maps *slavemap.Catalog, transport modbusrtu.Transport, options Options) *Service {
	if options.ScanFrom == 0 {
		options.ScanFrom = 1
	}
	if options.ScanTo == 0 || options.ScanTo < options.ScanFrom {
		options.ScanTo = 40
	}

	s := &Service{
		cfg:          cfg,
		maps:         maps,
		transport:    transport,
		options:      options,
		ops:          make(chan operation, 128),
		points:       make(map[string]PointState),
		slaves:       make(map[uint8]SlaveState),
		startedAt:    time.Now().UTC(),
		reqByID:      make(map[uint16]config.Request),
		moduleByID:   make(map[uint16]config.Module),
		moduleBySlave: make(map[uint8]config.Module),
		pointsByReq:  make(map[uint16][]config.Point),
		semanticByPI: make(map[uint16]SemanticPointRef),
	}
	s.indexConfig()
	return s
}

func (s *Service) SlaveMaps() []slavemap.Map {
	return s.maps.List()
}

func (s *Service) Start(ctx context.Context) error {
	for _, module := range s.cfg.Topology.Modules {
		if module.SlaveID == 0 {
			continue
		}
		s.slaves[module.SlaveID] = SlaveState{
			SlaveID:        module.SlaveID,
			ModuleID:       module.ModuleID,
			ModuleType:     module.ModuleType,
			ZoneID:         module.ZoneID,
			CapabilityMask: module.CapabilityMask,
		}
	}

	go s.modbusWorker(ctx)
	go s.pollLoop(ctx)
	return nil
}

func (s *Service) Close() error {
	return s.transport.Close()
}

func (s *Service) Snapshot() Snapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()

	slaves := make([]SlaveState, 0, len(s.slaves))
	for _, slave := range s.slaves {
		slaves = append(slaves, slave)
	}
	sort.Slice(slaves, func(i, j int) bool { return slaves[i].SlaveID < slaves[j].SlaveID })

	points := make([]PointState, 0, len(s.points))
	for _, point := range s.points {
		points = append(points, point)
	}
	sort.Slice(points, func(i, j int) bool { return points[i].PublishIndex < points[j].PublishIndex })

	return Snapshot{
		StartedAt:  s.startedAt,
		TopologyID: s.cfg.Topology.TopologyID,
		Generation: s.cfg.Topology.Generation,
		Slaves:     slaves,
		Points:     points,
		Modules:    s.cfg.Semantics.Modules,
	}
}

func (s *Service) LastScan() ScanResult {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.lastScan
}

func (s *Service) ApplySetpoint(ctx context.Context, req SetpointRequest) SetpointResult {
	if req.Key != "" {
		return s.applyMapSetpoint(ctx, req)
	}
	if req.SlaveID == 0 || req.ModuleID == 0 || req.CmdProfileID == 0 {
		return SetpointResult{Error: "slave_id and key are required for map setpoint writes; legacy writes require slave_id, module_id and cmd_profile_id"}
	}
	if len(req.Values) == 0 || len(req.Values) > 123 {
		return SetpointResult{Error: "values length must be 1..123"}
	}
	module, ok := s.moduleByID[req.ModuleID]
	if !ok || module.SlaveID != req.SlaveID {
		return SetpointResult{Error: "module/slave pair is not in topology"}
	}
	cmd, ok := s.findCommand(module, req.CmdProfileID)
	if !ok {
		return SetpointResult{Error: "command profile is not in topology"}
	}
	if uint16(len(req.Values)) > cmd.MaxRegCount {
		return SetpointResult{Error: fmt.Sprintf("too many values for command profile: got %d max %d", len(req.Values), cmd.MaxRegCount)}
	}

	values, err := uint16Values(req.Values)
	if err != nil {
		return SetpointResult{Error: err.Error()}
	}

	err = s.enqueue(ctx, func(opCtx context.Context) error {
		opCtx, cancel := context.WithTimeout(opCtx, commandTimeout(cmd))
		defer cancel()
		log.Printf("setpoint legacy slave=%d module=%d cmd_profile=%d start_reg=%d values=%v", req.SlaveID, req.ModuleID, req.CmdProfileID, cmd.StartReg, values)
		if cmd.FC == config.FCWriteSingle {
			if len(values) != 1 {
				return errors.New("single-register command requires exactly one value")
			}
			return s.transport.WriteSingle(opCtx, req.SlaveID, cmd.StartReg, values[0])
		}
		return s.transport.WriteMultiple(opCtx, req.SlaveID, cmd.StartReg, values)
	})
	if err != nil {
		return SetpointResult{Error: err.Error()}
	}
	return SetpointResult{
		Applied: true,
		Mode:    "legacy",
		SlaveID: req.SlaveID,
		Values:  values,
	}
}

func (s *Service) applyMapSetpoint(ctx context.Context, req SetpointRequest) SetpointResult {
	if req.SlaveID == 0 {
		return SetpointResult{Error: "slave_id is required"}
	}
	if req.Value != nil && len(req.Values) > 0 {
		return SetpointResult{Error: "use either value or values, not both"}
	}
	values := req.Values
	if req.Value != nil {
		values = []int{*req.Value}
	}
	if len(values) == 0 || len(values) > 123 {
		return SetpointResult{Error: "value or values length must be 1..123"}
	}

	m, _, err := s.mapForSlave(ctx, req.SlaveID)
	if err != nil {
		return SetpointResult{Error: err.Error()}
	}
	reg, ok := m.RegisterByKey(req.Key)
	if !ok {
		return SetpointResult{Error: fmt.Sprintf("key %q is not in %s", req.Key, m.Name)}
	}
	if !reg.IsWritable() {
		return SetpointResult{Error: fmt.Sprintf("key %q is read-only", req.Key)}
	}
	start, _ := reg.Bounds()
	if uint16(len(values)) > reg.Width() {
		return SetpointResult{Error: fmt.Sprintf("too many values for key %q: got %d max %d", req.Key, len(values), reg.Width())}
	}
	if !m.IsWritable(start, uint16(len(values))) {
		return SetpointResult{Error: fmt.Sprintf("register range %d..%d is read-only by write-policy", start, start+uint16(len(values))-1)}
	}
	words, err := uint16Values(values)
	if err != nil {
		return SetpointResult{Error: err.Error()}
	}

	err = s.enqueue(ctx, func(opCtx context.Context) error {
		opCtx, cancel := context.WithTimeout(opCtx, 1500*time.Millisecond)
		defer cancel()
		log.Printf("setpoint map slave=%d key=%s register=%d values=%v", req.SlaveID, req.Key, start, words)
		if len(words) == 1 {
			return s.transport.WriteSingle(opCtx, req.SlaveID, start, words[0])
		}
		return s.transport.WriteMultiple(opCtx, req.SlaveID, start, words)
	})
	if err != nil {
		return SetpointResult{Error: err.Error()}
	}
	return SetpointResult{
		Applied:  true,
		Mode:     "map",
		SlaveID:  req.SlaveID,
		Key:      req.Key,
		Register: &start,
		Values:   words,
	}
}

func (s *Service) Scan(ctx context.Context, from, to uint8) ScanResult {
	if from == 0 {
		from = s.options.ScanFrom
	}
	if to == 0 || to < from {
		to = s.options.ScanTo
	}
	result := ScanResult{
		StartedAt: time.Now().UTC(),
		From:      from,
		To:        to,
		Errors:    make(map[uint8]string),
	}
	for address := from; address <= to; address++ {
		addr := address
		var regs []uint16
		err := s.enqueue(ctx, func(opCtx context.Context) error {
			opCtx, cancel := context.WithTimeout(opCtx, 1500*time.Millisecond)
			defer cancel()
			var readErr error
			regs, readErr = s.transport.ReadHolding(opCtx, addr, 0, 16)
			return readErr
		})
		if err != nil {
			result.Errors[addr] = err.Error()
		} else if len(regs) >= 16 {
			m, supported := s.maps.Find(regs[0], regs[2])
			mapName := ""
			if supported {
				mapName = m.Name
			}
			_, expected := s.moduleBySlave[addr]
			result.Devices = append(result.Devices, DiscoveredDevice{
				Address:          addr,
				Expected:         expected,
				DeviceType:       regs[0],
				FirmwareVersion:  regs[1],
				ModbusMapVersion: regs[2],
				ZoneID:           regs[3],
				CapabilityMask:   uint32(regs[4]) | (uint32(regs[5]) << 16),
				Status:           regs[6],
				FaultCode:        regs[7],
				MapName:          mapName,
				Supported:        supported,
				SeenAt:           time.Now().UTC(),
			})
		}
		if addr == math.MaxUint8 {
			break
		}
	}
	result.FinishedAt = time.Now().UTC()

	s.mu.Lock()
	s.lastScan = result
	s.mu.Unlock()
	return result
}

func (s *Service) indexConfig() {
	for _, module := range s.cfg.Topology.Modules {
		s.moduleByID[module.ModuleID] = module
		if module.SlaveID != 0 {
			s.moduleBySlave[module.SlaveID] = module
		}
	}
	for _, req := range s.cfg.Topology.Requests {
		s.reqByID[req.ReqID] = req
	}
	for _, point := range s.cfg.Topology.Points {
		s.pointsByReq[point.ReqID] = append(s.pointsByReq[point.ReqID], point)
	}
	for key, point := range s.cfg.Semantics.Points {
		s.semanticByPI[point.PublishIndex] = SemanticPointRef{Key: key, Point: point}
	}
}

func (s *Service) modbusWorker(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case op := <-s.ops:
			op.done <- op.run(ctx)
			close(op.done)
		}
	}
}

func (s *Service) pollLoop(ctx context.Context) {
	type dueModule struct {
		module config.Module
		next time.Time
	}
	var plan []dueModule
	now := time.Now()
	for _, module := range s.cfg.Topology.Modules {
		if module.SlaveID == 0 {
			continue
		}
		plan = append(plan, dueModule{module: module, next: now})
	}

	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case now = <-ticker.C:
			for i := range plan {
				if now.Before(plan[i].next) {
					continue
				}
				module := plan[i].module
				period := modulePollPeriod(module)
				plan[i].next = now.Add(period)
				go s.pollModule(ctx, module)
			}
		}
	}
}

func (s *Service) pollModule(ctx context.Context, module config.Module) {
	m, identity, err := s.mapForSlave(ctx, module.SlaveID)
	if err != nil {
		s.markSlaveError(module, err)
		return
	}
	regs := m.PollRegisters()
	if len(regs) == 0 {
		s.publishSlaveIdentity(module, identity, m)
		return
	}

	values := make(map[uint16]uint16)
	for _, span := range readSpans(regs) {
		start := span.from
		count := span.to - span.from + 1
		var words []uint16
		err := s.enqueue(ctx, func(opCtx context.Context) error {
			opCtx, cancel := context.WithTimeout(opCtx, 1500*time.Millisecond)
			defer cancel()
			var readErr error
			words, readErr = s.transport.ReadHolding(opCtx, module.SlaveID, start, count)
			return readErr
		})
		if err != nil {
			s.markSlaveError(module, err)
			return
		}
		for i, word := range words {
			values[start+uint16(i)] = word
		}
	}
	s.publishMapRegisters(module, identity, m, regs, values)
}

type slaveIdentity struct {
	DeviceType       uint16
	FirmwareVersion  uint16
	ModbusMapVersion uint16
	ZoneID           uint16
	CapabilityMask   uint32
	Status           uint16
	FaultCode        uint16
}

type readSpan struct {
	from uint16
	to   uint16
}

func (s *Service) mapForSlave(ctx context.Context, slaveID uint8) (slavemap.Map, slaveIdentity, error) {
	var regs []uint16
	err := s.enqueue(ctx, func(opCtx context.Context) error {
		opCtx, cancel := context.WithTimeout(opCtx, 1500*time.Millisecond)
		defer cancel()
		var readErr error
		regs, readErr = s.transport.ReadHolding(opCtx, slaveID, 0, 16)
		return readErr
	})
	if err != nil {
		return slavemap.Map{}, slaveIdentity{}, err
	}
	if len(regs) < 16 {
		return slavemap.Map{}, slaveIdentity{}, fmt.Errorf("identity read returned %d registers, want 16", len(regs))
	}
	identity := slaveIdentity{
		DeviceType:       regs[0],
		FirmwareVersion:  regs[1],
		ModbusMapVersion: regs[2],
		ZoneID:           regs[3],
		CapabilityMask:   uint32(regs[4]) | (uint32(regs[5]) << 16),
		Status:           regs[6],
		FaultCode:        regs[7],
	}
	m, ok := s.maps.Find(identity.DeviceType, identity.ModbusMapVersion)
	if !ok {
		return slavemap.Map{}, identity, fmt.Errorf("unsupported slave map device_type=%d modbus_map_version=%d", identity.DeviceType, identity.ModbusMapVersion)
	}
	return m, identity, nil
}

func readSpans(regs []slavemap.Register) []readSpan {
	spans := make([]readSpan, 0, len(regs))
	for _, reg := range regs {
		from, to := reg.Bounds()
		spans = append(spans, readSpan{from: from, to: to})
	}
	sort.Slice(spans, func(i, j int) bool {
		if spans[i].from == spans[j].from {
			return spans[i].to < spans[j].to
		}
		return spans[i].from < spans[j].from
	})
	merged := spans[:0]
	for _, span := range spans {
		if len(merged) == 0 || span.from > merged[len(merged)-1].to+1 {
			merged = append(merged, span)
			continue
		}
		if span.to > merged[len(merged)-1].to {
			merged[len(merged)-1].to = span.to
		}
	}
	return merged
}

func (s *Service) publishMapRegisters(module config.Module, identity slaveIdentity, m slavemap.Map, regs []slavemap.Register, values map[uint16]uint16) {
	now := time.Now().UTC()
	s.mu.Lock()
	defer s.mu.Unlock()

	s.updateSlaveLocked(module, identity, m, now)

	for _, reg := range regs {
		start, end := reg.Bounds()
		if start != end {
			continue
		}
		raw, ok := values[start]
		if !ok {
			continue
		}
		pointKey := fmt.Sprintf("slave_%d.%s", module.SlaveID, reg.Key)
		s.points[pointKey] = PointState{
			Key:          reg.Key,
			ModuleID:     module.ModuleID,
			SlaveID:      module.SlaveID,
			ZoneID:       identity.ZoneID,
			PublishIndex: start,
			Register:     start,
			MapName:      m.Name,
			Label:        reg.Key,
			Unit:         reg.Unit,
			Type:         reg.Type,
			ValueRaw:     uint32(raw),
			Value:        decodedValue(raw, reg),
			Quality:      QualityOK,
			UpdatedAt:    now,
		}
	}
}

func (s *Service) publishSlaveIdentity(module config.Module, identity slaveIdentity, m slavemap.Map) {
	now := time.Now().UTC()
	s.mu.Lock()
	defer s.mu.Unlock()
	s.updateSlaveLocked(module, identity, m, now)
}

func (s *Service) updateSlaveLocked(module config.Module, identity slaveIdentity, m slavemap.Map, now time.Time) {
	slave := s.slaves[module.SlaveID]
	slave.Online = true
	slave.LastOKAt = now
	slave.LastError = ""
	slave.FailCount = 0
	slave.DeviceType = identity.DeviceType
	slave.ModbusMapVersion = identity.ModbusMapVersion
	slave.ZoneID = identity.ZoneID
	slave.CapabilityMask = identity.CapabilityMask
	slave.MapName = m.Name
	slave.Supported = true
	s.slaves[module.SlaveID] = slave
}

func (s *Service) pollRequest(ctx context.Context, req config.Request) {
	module, ok := s.moduleByID[req.ModuleID]
	if !ok || module.SlaveID == 0 {
		return
	}
	var regs []uint16
	err := s.enqueue(ctx, func(opCtx context.Context) error {
		opCtx, cancel := context.WithTimeout(opCtx, requestTimeout(req))
		defer cancel()
		var readErr error
		regs, readErr = s.transport.ReadHolding(opCtx, module.SlaveID, req.StartReg, req.RegCount)
		return readErr
	})
	if err != nil {
		s.markSlaveError(module, err)
		return
	}
	s.publishRequest(module, req, regs)
}

func (s *Service) enqueue(ctx context.Context, fn func(context.Context) error) error {
	op := operation{run: fn, done: make(chan error, 1)}
	select {
	case <-ctx.Done():
		return ctx.Err()
	case s.ops <- op:
	}
	select {
	case <-ctx.Done():
		return ctx.Err()
	case err := <-op.done:
		return err
	}
}

func (s *Service) publishRequest(module config.Module, req config.Request, regs []uint16) {
	now := time.Now().UTC()
	s.mu.Lock()
	defer s.mu.Unlock()

	slave := s.slaves[module.SlaveID]
	slave.Online = true
	slave.LastOKAt = now
	slave.LastError = ""
	slave.FailCount = 0
	s.slaves[module.SlaveID] = slave

	for _, point := range s.pointsByReq[req.ReqID] {
		if int(point.RegOffset) >= len(regs) {
			continue
		}
		raw := uint32(regs[point.RegOffset])
		value := scaledValue(raw, point.ScalePow10, point.PointType)
		sem := s.semanticByPI[point.PublishIndex]
		key := sem.Key
		label := sem.Point.Label
		unit := sem.Point.Unit
		if key == "" {
			key = fmt.Sprintf("point_%d", point.PointID)
		}
		stateKey := fmt.Sprintf("legacy_%d", point.PublishIndex)
		s.points[stateKey] = PointState{
			Key:          key,
			PointID:      point.PointID,
			ModuleID:     module.ModuleID,
			SlaveID:      module.SlaveID,
			ZoneID:       module.ZoneID,
			PublishIndex: point.PublishIndex,
			Label:        label,
			Unit:         unit,
			ValueRaw:     raw,
			Value:        value,
			Quality:      QualityOK,
			UpdatedAt:    now,
		}
	}
}

func (s *Service) markSlaveError(module config.Module, err error) {
	now := time.Now().UTC()
	log.Printf("poll slave=%d module=%d failed: %v", module.SlaveID, module.ModuleID, err)
	s.mu.Lock()
	defer s.mu.Unlock()

	slave := s.slaves[module.SlaveID]
	slave.Online = false
	slave.LastErrorAt = now
	slave.LastError = err.Error()
	slave.FailCount++
	s.slaves[module.SlaveID] = slave

	for idx, point := range s.points {
		if point.SlaveID != module.SlaveID {
			continue
		}
		point.Quality = QualityStale
		point.Error = err.Error()
		s.points[idx] = point
	}
}

func (s *Service) findCommand(module config.Module, cmdID uint16) (config.Command, bool) {
	end := int(module.CmdFirst) + int(module.CmdCount)
	if end > len(s.cfg.Topology.Commands) {
		end = len(s.cfg.Topology.Commands)
	}
	for i := int(module.CmdFirst); i < end; i++ {
		cmd := s.cfg.Topology.Commands[i]
		if cmd.ModuleID == module.ModuleID && cmd.CmdID == cmdID {
			return cmd, true
		}
	}
	return config.Command{}, false
}

func requestTimeout(req config.Request) time.Duration {
	timeout := time.Duration(req.TimeoutMS) * time.Millisecond
	if timeout <= 0 {
		timeout = 300 * time.Millisecond
	}
	retries := int(req.Retries) + 1
	backoff := time.Duration(req.BackoffMS) * time.Millisecond
	return time.Duration(retries)*timeout + time.Duration(retries)*backoff
}

func modulePollPeriod(module config.Module) time.Duration {
	if module.HeartbeatTimeoutMS > 0 {
		period := time.Duration(module.HeartbeatTimeoutMS) * time.Millisecond / 2
		if period >= time.Second {
			return period
		}
	}
	return 2 * time.Second
}

func commandTimeout(cmd config.Command) time.Duration {
	timeout := time.Duration(cmd.TimeoutMS) * time.Millisecond
	if timeout <= 0 {
		timeout = 500 * time.Millisecond
	}
	return time.Duration(int(cmd.Retries)+1)*timeout + 200*time.Millisecond
}

func decodedValue(raw uint16, reg slavemap.Register) float64 {
	var value float64
	switch reg.Type {
	case "s16":
		value = float64(int16(raw))
	default:
		value = float64(raw)
	}
	if reg.Scale != nil {
		value *= *reg.Scale
	}
	return value
}

func scaledValue(raw uint32, scalePow10 int8, pointType uint8) float64 {
	var signed float64
	switch pointType {
	case 2:
		signed = float64(int16(raw))
	default:
		signed = float64(raw)
	}
	scale := math.Pow10(int(scalePow10))
	return signed * scale
}

func uint16Values(values []int) ([]uint16, error) {
	out := make([]uint16, len(values))
	for i, value := range values {
		if value < -32768 || value > 65535 {
			return nil, fmt.Errorf("value[%d]=%d is outside Modbus register range", i, value)
		}
		out[i] = uint16(int16(value))
		if value >= 0 {
			out[i] = uint16(value)
		}
	}
	return out, nil
}
