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
	transport modbusrtu.Transport
	options   Options

	ops chan operation

	mu        sync.RWMutex
	points    map[uint16]PointState
	slaves    map[uint8]SlaveState
	lastScan  ScanResult
	startedAt time.Time

	reqByID      map[uint16]config.Request
	moduleByID   map[uint16]config.Module
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
	Label        string    `json:"label"`
	Unit         string    `json:"unit"`
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
	Values       []uint16 `json:"values"`
}

type SetpointResult struct {
	Applied bool   `json:"applied"`
	Error   string `json:"error,omitempty"`
}

type DiscoveredDevice struct {
	Address          uint8     `json:"address"`
	DeviceType       uint16    `json:"device_type"`
	FirmwareVersion  uint16    `json:"firmware_version"`
	ModbusMapVersion uint16    `json:"modbus_map_version"`
	ZoneID           uint16    `json:"zone_id"`
	CapabilityMask   uint32    `json:"capability_mask"`
	Status           uint16    `json:"status"`
	FaultCode        uint16    `json:"fault_code"`
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

func NewService(cfg *config.Config, transport modbusrtu.Transport, options Options) *Service {
	if options.ScanFrom == 0 {
		options.ScanFrom = 1
	}
	if options.ScanTo == 0 || options.ScanTo < options.ScanFrom {
		options.ScanTo = 40
	}

	s := &Service{
		cfg:          cfg,
		transport:    transport,
		options:      options,
		ops:          make(chan operation, 128),
		points:       make(map[uint16]PointState),
		slaves:       make(map[uint8]SlaveState),
		startedAt:    time.Now().UTC(),
		reqByID:      make(map[uint16]config.Request),
		moduleByID:   make(map[uint16]config.Module),
		pointsByReq:  make(map[uint16][]config.Point),
		semanticByPI: make(map[uint16]SemanticPointRef),
	}
	s.indexConfig()
	return s
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
	if req.SlaveID == 0 || req.ModuleID == 0 || req.CmdProfileID == 0 {
		return SetpointResult{Error: "slave_id, module_id and cmd_profile_id are required"}
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

	err := s.enqueue(ctx, func(opCtx context.Context) error {
		opCtx, cancel := context.WithTimeout(opCtx, commandTimeout(cmd))
		defer cancel()
		if cmd.FC == config.FCWriteSingle {
			if len(req.Values) != 1 {
				return errors.New("single-register command requires exactly one value")
			}
			return s.transport.WriteSingle(opCtx, req.SlaveID, cmd.StartReg, req.Values[0])
		}
		return s.transport.WriteMultiple(opCtx, req.SlaveID, cmd.StartReg, req.Values)
	})
	if err != nil {
		return SetpointResult{Error: err.Error()}
	}
	return SetpointResult{Applied: true}
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
			regs, readErr = s.transport.ReadHolding(opCtx, addr, 0, 8)
			return readErr
		})
		if err != nil {
			result.Errors[addr] = err.Error()
		} else if len(regs) >= 8 {
			result.Devices = append(result.Devices, DiscoveredDevice{
				Address:          addr,
				DeviceType:       regs[0],
				FirmwareVersion:  regs[1],
				ModbusMapVersion: regs[2],
				ZoneID:           regs[3],
				CapabilityMask:   uint32(regs[4]) | (uint32(regs[5]) << 16),
				Status:           regs[6],
				FaultCode:        regs[7],
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
	type dueReq struct {
		req config.Request
		next time.Time
	}
	var plan []dueReq
	now := time.Now()
	for _, req := range s.cfg.Topology.Requests {
		if req.FC != config.FCReadHolding {
			continue
		}
		plan = append(plan, dueReq{req: req, next: now})
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
				req := plan[i].req
				period := time.Duration(req.PeriodMS) * time.Millisecond
				if period <= 0 {
					period = 5 * time.Second
				}
				plan[i].next = now.Add(period)
				go s.pollRequest(ctx, req)
			}
		}
	}
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
		s.points[point.PublishIndex] = PointState{
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

func commandTimeout(cmd config.Command) time.Duration {
	timeout := time.Duration(cmd.TimeoutMS) * time.Millisecond
	if timeout <= 0 {
		timeout = 500 * time.Millisecond
	}
	return time.Duration(int(cmd.Retries)+1)*timeout + 200*time.Millisecond
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
