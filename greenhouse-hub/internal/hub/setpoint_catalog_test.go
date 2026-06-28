package hub

import (
	"context"
	"path/filepath"
	"testing"

	"greenhouse-hub/internal/config"
	"greenhouse-hub/internal/modbusrtu"
	"greenhouse-hub/internal/slavemap"
)

func newSetpointCatalogTestService(t *testing.T, capabilityMask uint32) (*Service, context.CancelFunc) {
	t.Helper()

	cfg := &config.Config{
		Topology: config.Topology{
			Modules: []config.Module{
				{
					ModuleID:       101,
					ModuleType:     config.ModuleTypeZone,
					SlaveID:        1,
					ZoneID:         1,
					CapabilityMask: capabilityMask,
				},
			},
		},
	}
	maps, err := slavemap.LoadCatalog(filepath.Join("..", "..", "slave_maps"))
	if err != nil {
		t.Fatalf("LoadCatalog() error = %v", err)
	}
	service := NewService(cfg, maps, modbusrtu.NewMockTransport(cfg.Topology), Options{})
	ctx, cancel := context.WithCancel(context.Background())
	if err := service.Start(ctx); err != nil {
		cancel()
		t.Fatalf("Start() error = %v", err)
	}
	return service, cancel
}

func TestSetpointsForSlaveFiltersByCapability(t *testing.T) {
	service, cancel := newSetpointCatalogTestService(t, 1<<10)
	defer cancel()

	catalog, err := service.SetpointsForSlave(context.Background(), 1, false)
	if err != nil {
		t.Fatalf("SetpointsForSlave() error = %v", err)
	}

	if group := findSetpointGroup(catalog.Groups, "heating"); group == nil || !group.Supported {
		t.Fatalf("heating group missing or unsupported: %#v", group)
	}
	if group := findSetpointGroup(catalog.Groups, "circulation"); group == nil || !group.Supported {
		t.Fatalf("circulation group missing or unsupported: %#v", group)
	}
	if group := findSetpointGroup(catalog.Groups, "water"); group != nil {
		t.Fatalf("water group returned without water capabilities: %#v", group)
	}
}

func TestSetpointsForSlaveIncludeUnsupported(t *testing.T) {
	service, cancel := newSetpointCatalogTestService(t, 1<<10)
	defer cancel()

	catalog, err := service.SetpointsForSlave(context.Background(), 1, true)
	if err != nil {
		t.Fatalf("SetpointsForSlave() error = %v", err)
	}

	group := findSetpointGroup(catalog.Groups, "water")
	if group == nil {
		t.Fatal("water group missing with includeUnsupported=true")
	}
	if group.Supported {
		t.Fatal("water group Supported = true, want false")
	}
	if item := findSetpointItem(group.Items, "water_rail_setpoint"); item == nil {
		t.Fatal("water_rail_setpoint missing")
	}
}

func TestSetpointsForSlaveHidesServiceFieldsAndAddsEnums(t *testing.T) {
	service, cancel := newSetpointCatalogTestService(t, (1<<2)|(1<<7)|(1<<10)|(1<<11)|(1<<12))
	defer cancel()

	catalog, err := service.SetpointsForSlave(context.Background(), 1, false)
	if err != nil {
		t.Fatalf("SetpointsForSlave() error = %v", err)
	}

	for _, group := range catalog.Groups {
		for _, item := range group.Items {
			switch item.Key {
			case "command_setpoint_payload", "command_token", "weather_token", "co2_fault_reset_token":
				t.Fatalf("service key %q should be hidden", item.Key)
			}
			if item.Reg == 0 || item.Width == 0 {
				t.Fatalf("item %q has invalid reg/width: %#v", item.Key, item)
			}
		}
	}

	windows := findSetpointGroup(catalog.Groups, "windows")
	if windows == nil {
		t.Fatal("windows group missing")
	}
	mode := findSetpointItem(windows.Items, "windows_ctrl_mode")
	if mode == nil {
		t.Fatal("windows_ctrl_mode missing")
	}
	if len(mode.Enum) != 2 || mode.Enum[0].Label != "Auto" {
		t.Fatalf("windows_ctrl_mode enum = %#v", mode.Enum)
	}
}

func TestSetpointsForSlaveUnknownSlave(t *testing.T) {
	service, cancel := newSetpointCatalogTestService(t, 0)
	defer cancel()

	if _, err := service.SetpointsForSlave(context.Background(), 2, false); err != ErrSlaveNotFound {
		t.Fatalf("SetpointsForSlave() error = %v, want %v", err, ErrSlaveNotFound)
	}
}

func findSetpointGroup(groups []SetpointGroup, key string) *SetpointGroup {
	for i := range groups {
		if groups[i].Key == key {
			return &groups[i]
		}
	}
	return nil
}

func findSetpointItem(items []SetpointItem, key string) *SetpointItem {
	for i := range items {
		if items[i].Key == key {
			return &items[i]
		}
	}
	return nil
}
