package modbusrtu

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/goburrow/modbus"

	"greenhouse-hub/internal/config"
)

type Transport interface {
	ReadHolding(ctx context.Context, slaveID uint8, startReg uint16, count uint16) ([]uint16, error)
	WriteSingle(ctx context.Context, slaveID uint8, reg uint16, value uint16) error
	WriteMultiple(ctx context.Context, slaveID uint8, startReg uint16, values []uint16) error
	Close() error
}

type RTUConfig struct {
	Device  string
	Baud    int
	Timeout time.Duration
}

type RTUTransport struct {
	handler *modbus.RTUClientHandler
	mu      sync.Mutex
}

func NewRTUTransport(cfg RTUConfig) *RTUTransport {
	handler := modbus.NewRTUClientHandler(cfg.Device)
	handler.BaudRate = cfg.Baud
	handler.DataBits = 8
	handler.Parity = "N"
	handler.StopBits = 1
	handler.Timeout = cfg.Timeout
	return &RTUTransport{handler: handler}
}

func (t *RTUTransport) ReadHolding(ctx context.Context, slaveID uint8, startReg uint16, count uint16) ([]uint16, error) {
	if count == 0 || count > 125 {
		return nil, fmt.Errorf("read holding count out of range: %d", count)
	}
	var out []uint16
	err := t.withClient(ctx, slaveID, func(client modbus.Client) error {
		raw, err := client.ReadHoldingRegisters(startReg, count)
		if err != nil {
			return err
		}
		if len(raw) != int(count)*2 {
			return fmt.Errorf("unexpected byte count %d for %d registers", len(raw), count)
		}
		out = wordsFromBytes(raw)
		return nil
	})
	return out, err
}

func (t *RTUTransport) WriteSingle(ctx context.Context, slaveID uint8, reg uint16, value uint16) error {
	return t.withClient(ctx, slaveID, func(client modbus.Client) error {
		_, err := client.WriteSingleRegister(reg, value)
		return err
	})
}

func (t *RTUTransport) WriteMultiple(ctx context.Context, slaveID uint8, startReg uint16, values []uint16) error {
	if len(values) == 0 || len(values) > 123 {
		return fmt.Errorf("write multiple count out of range: %d", len(values))
	}
	payload := make([]byte, 0, len(values)*2)
	for _, value := range values {
		payload = append(payload, byte(value>>8), byte(value))
	}
	return t.withClient(ctx, slaveID, func(client modbus.Client) error {
		_, err := client.WriteMultipleRegisters(startReg, uint16(len(values)), payload)
		return err
	})
}

func (t *RTUTransport) Close() error {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.handler.Close()
}

func (t *RTUTransport) withClient(ctx context.Context, slaveID uint8, fn func(modbus.Client) error) error {
	done := make(chan error, 1)
	go func() {
		t.mu.Lock()
		defer t.mu.Unlock()
		t.handler.SlaveId = slaveID
		if err := t.handler.Connect(); err != nil {
			done <- err
			return
		}
		defer t.handler.Close()
		done <- fn(modbus.NewClient(t.handler))
	}()

	select {
	case <-ctx.Done():
		return ctx.Err()
	case err := <-done:
		return err
	}
}

func wordsFromBytes(raw []byte) []uint16 {
	words := make([]uint16, len(raw)/2)
	for i := range words {
		words[i] = uint16(raw[i*2])<<8 | uint16(raw[i*2+1])
	}
	return words
}

type MockTransport struct {
	mu   sync.Mutex
	regs map[uint8]map[uint16]uint16
}

func NewMockTransport(topology config.Topology) *MockTransport {
	regs := make(map[uint8]map[uint16]uint16)
	for _, module := range topology.Modules {
		if module.SlaveID == 0 {
			continue
		}
		if regs[module.SlaveID] == nil {
			regs[module.SlaveID] = make(map[uint16]uint16)
		}
		regs[module.SlaveID][0] = uint16(module.ModuleType)
		regs[module.SlaveID][1] = 1
		regs[module.SlaveID][2] = uint16(topology.VerMinor)
		regs[module.SlaveID][3] = module.ZoneID
		regs[module.SlaveID][4] = uint16(module.CapabilityMask & 0xFFFF)
		regs[module.SlaveID][5] = uint16(module.CapabilityMask >> 16)
		regs[module.SlaveID][6] = 1
		regs[module.SlaveID][7] = 0
	}
	for _, point := range topology.Points {
		module, ok := findModule(topology.Modules, point.ModuleID)
		if !ok || module.SlaveID == 0 {
			continue
		}
		req, ok := findRequest(topology.Requests, point.ReqID)
		if !ok {
			continue
		}
		regs[module.SlaveID][req.StartReg+point.RegOffset] = uint16(point.PublishIndex + 100)
	}
	return &MockTransport{regs: regs}
}

func (t *MockTransport) ReadHolding(ctx context.Context, slaveID uint8, startReg uint16, count uint16) ([]uint16, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	default:
	}
	slaveRegs, ok := t.regs[slaveID]
	if !ok {
		return nil, fmt.Errorf("mock slave %d not found", slaveID)
	}
	out := make([]uint16, count)
	for i := range out {
		out[i] = slaveRegs[startReg+uint16(i)]
	}
	return out, nil
}

func (t *MockTransport) WriteSingle(ctx context.Context, slaveID uint8, reg uint16, value uint16) error {
	return t.WriteMultiple(ctx, slaveID, reg, []uint16{value})
}

func (t *MockTransport) WriteMultiple(ctx context.Context, slaveID uint8, startReg uint16, values []uint16) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	select {
	case <-ctx.Done():
		return ctx.Err()
	default:
	}
	if t.regs[slaveID] == nil {
		return fmt.Errorf("mock slave %d not found", slaveID)
	}
	for i, value := range values {
		t.regs[slaveID][startReg+uint16(i)] = value
	}
	return nil
}

func (t *MockTransport) Close() error { return nil }

func findModule(modules []config.Module, id uint16) (config.Module, bool) {
	for _, module := range modules {
		if module.ModuleID == id {
			return module, true
		}
	}
	return config.Module{}, false
}

func findRequest(requests []config.Request, id uint16) (config.Request, bool) {
	for _, req := range requests {
		if req.ReqID == id {
			return req, true
		}
	}
	return config.Request{}, false
}
