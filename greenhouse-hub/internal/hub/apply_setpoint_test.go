package hub

import (
	"context"
	"path/filepath"
	"testing"

	"greenhouse-hub/internal/config"
	"greenhouse-hub/internal/modbusrtu"
	"greenhouse-hub/internal/slavemap"
)

func newApplySetpointTestService(t *testing.T, cfg *config.Config, maps *slavemap.Catalog) (*Service, *modbusrtu.MockTransport, context.CancelFunc) {
	t.Helper()

	transport := modbusrtu.NewMockTransport(cfg.Topology)
	service := NewService(cfg, maps, transport, Options{})
	ctx, cancel := context.WithCancel(context.Background())
	go service.modbusWorker(ctx)
	return service, transport, cancel
}

func TestApplyMapSetpointWritesWindowsCtrlModeAt1030(t *testing.T) {
	maps, err := slavemap.LoadCatalog(filepath.Join("..", "..", "slave_maps"))
	if err != nil {
		t.Fatalf("LoadCatalog() error = %v", err)
	}
	cfg := &config.Config{
		Topology: config.Topology{
			Modules: []config.Module{
				{
					ModuleID:       101,
					ModuleType:     config.ModuleTypeZone,
					SlaveID:        1,
					ZoneID:         1,
					CapabilityMask: 0xFFFFFFFF,
				},
			},
		},
	}
	service, transport, cancel := newApplySetpointTestService(t, cfg, maps)
	defer cancel()

	value := 1
	result := service.ApplySetpoint(context.Background(), SetpointRequest{
		SlaveID: 1,
		Key:     "windows_ctrl_mode",
		Value:   &value,
	})
	if !result.Applied {
		t.Fatalf("ApplySetpoint() not applied: %s", result.Error)
	}
	if result.Mode != "map" {
		t.Fatalf("ApplySetpoint() mode = %q, want map", result.Mode)
	}
	if result.Register == nil || *result.Register != 1030 {
		t.Fatalf("ApplySetpoint() register = %v, want 1030", result.Register)
	}

	regs, err := transport.ReadHolding(context.Background(), 1, 1030, 2)
	if err != nil {
		t.Fatalf("ReadHolding() error = %v", err)
	}
	if regs[0] != uint16(value) {
		t.Fatalf("register 1030 = %d, want %d", regs[0], value)
	}
	if regs[1] != 0 {
		t.Fatalf("register 1031 = %d, want 0; windows_ctrl_mode must not shift into windows_force_safe_cmd", regs[1])
	}
}

func TestApplyMapSetpointWritesWindowsPosATargetAt1005(t *testing.T) {
	maps, err := slavemap.LoadCatalog(filepath.Join("..", "..", "slave_maps"))
	if err != nil {
		t.Fatalf("LoadCatalog() error = %v", err)
	}
	cfg := &config.Config{
		Topology: config.Topology{
			Modules: []config.Module{
				{
					ModuleID:       101,
					ModuleType:     config.ModuleTypeZone,
					SlaveID:        1,
					ZoneID:         1,
					CapabilityMask: 0xFFFFFFFF,
				},
			},
		},
	}
	service, transport, cancel := newApplySetpointTestService(t, cfg, maps)
	defer cancel()

	value := 55
	result := service.ApplySetpoint(context.Background(), SetpointRequest{
		SlaveID: 1,
		Key:     "windows_pos_a_target",
		Value:   &value,
	})
	if !result.Applied {
		t.Fatalf("ApplySetpoint() not applied: %s", result.Error)
	}
	if result.Mode != "map" {
		t.Fatalf("ApplySetpoint() mode = %q, want map", result.Mode)
	}
	if result.Register == nil || *result.Register != 1005 {
		t.Fatalf("ApplySetpoint() register = %v, want 1005", result.Register)
	}

	regs, err := transport.ReadHolding(context.Background(), 1, 1005, 3)
	if err != nil {
		t.Fatalf("ReadHolding() error = %v", err)
	}
	if regs[0] != uint16(value) {
		t.Fatalf("register 1005 = %d, want %d", regs[0], value)
	}
	if regs[1] != 0 {
		t.Fatalf("register 1006 = %d, want 0; windows_pos_a_target must not shift into windows_pos_b_target", regs[1])
	}
	if regs[2] != 0 {
		t.Fatalf("register 1007 = %d, want 0; windows_pos_a_target must not shift into curtain_pos_target", regs[2])
	}
}

func TestApplyMapSetpointRejectsWeatherTelemetry(t *testing.T) {
	maps, err := slavemap.LoadCatalog(filepath.Join("..", "..", "slave_maps"))
	if err != nil {
		t.Fatalf("LoadCatalog() error = %v", err)
	}
	cfg := &config.Config{
		Topology: config.Topology{
			Modules: []config.Module{
				{
					ModuleID:       101,
					ModuleType:     config.ModuleTypeZone,
					SlaveID:        1,
					ZoneID:         1,
					CapabilityMask: 0xFFFFFFFF,
				},
			},
		},
	}
	service, _, cancel := newApplySetpointTestService(t, cfg, maps)
	defer cancel()

	value := 120
	result := service.ApplySetpoint(context.Background(), SetpointRequest{
		SlaveID: 1,
		Key:     "weather_wind_speed",
		Value:   &value,
	})
	if result.Applied {
		t.Fatalf("ApplySetpoint() applied weather telemetry as operator setpoint: %#v", result)
	}
	if result.Error != `key "weather_wind_speed" is not an operator setpoint` {
		t.Fatalf("ApplySetpoint() error = %q", result.Error)
	}
}

func TestApplyLegacyWindowsProfilePayloadZeroWrites1030(t *testing.T) {
	cfg := &config.Config{
		Topology: config.Topology{
			Modules: []config.Module{
				{
					ModuleID:   101,
					ModuleType: config.ModuleTypeZone,
					SlaveID:    1,
					ZoneID:     1,
					CmdFirst:   0,
					CmdCount:   1,
				},
			},
			Commands: []config.Command{
				{
					CmdID:       5004,
					ModuleID:    101,
					FC:          config.FCWriteMultiple,
					StartReg:    1030,
					MaxRegCount: 16,
					PayloadOff:  0,
					TimeoutMS:   500,
				},
			},
		},
	}
	service, transport, cancel := newApplySetpointTestService(t, cfg, slavemap.MustEmptyCatalog())
	defer cancel()

	result := service.ApplySetpoint(context.Background(), SetpointRequest{
		SlaveID:      1,
		ModuleID:     101,
		CmdProfileID: 5004,
		Values:       []int{7},
	})
	if !result.Applied {
		t.Fatalf("ApplySetpoint() not applied: %s", result.Error)
	}
	if result.Mode != "legacy" {
		t.Fatalf("ApplySetpoint() mode = %q, want legacy", result.Mode)
	}

	regs, err := transport.ReadHolding(context.Background(), 1, 1030, 2)
	if err != nil {
		t.Fatalf("ReadHolding() error = %v", err)
	}
	if regs[0] != 7 {
		t.Fatalf("register 1030 = %d, want 7", regs[0])
	}
	if regs[1] != 0 {
		t.Fatalf("register 1031 = %d, want 0; legacy payload[0] must map to start_reg 1030", regs[1])
	}
}
