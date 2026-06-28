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
