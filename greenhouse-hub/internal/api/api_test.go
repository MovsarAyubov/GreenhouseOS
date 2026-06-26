package api

import (
	"context"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"

	"greenhouse-hub/internal/config"
	"greenhouse-hub/internal/hub"
	"greenhouse-hub/internal/modbusrtu"
	"greenhouse-hub/internal/slavemap"
)

func newTestHandler(t *testing.T) (http.Handler, context.CancelFunc) {
	t.Helper()

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
	maps, err := slavemap.LoadCatalog(filepath.Join("..", "..", "slave_maps"))
	if err != nil {
		t.Fatalf("LoadCatalog() error = %v", err)
	}
	service := hub.NewService(cfg, maps, modbusrtu.NewMockTransport(cfg.Topology), hub.Options{})
	ctx, cancel := context.WithCancel(context.Background())
	if err := service.Start(ctx); err != nil {
		cancel()
		t.Fatalf("Start() error = %v", err)
	}
	return NewHandler(service), cancel
}

func TestSlaveSetpointsEndpoint(t *testing.T) {
	handler, cancel := newTestHandler(t)
	defer cancel()

	req := httptest.NewRequest(http.MethodGet, "/api/slaves/1/setpoints", nil)
	rec := httptest.NewRecorder()
	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d body=%s", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `"slave_id":1`) {
		t.Fatalf("response does not include slave_id: %s", body)
	}
	if !strings.Contains(body, `"key":"heating"`) || !strings.Contains(body, `"key":"heating_ctrl_mode"`) {
		t.Fatalf("response does not include heating setpoints: %s", body)
	}
}

func TestSlaveSetpointsEndpointUnknownSlave(t *testing.T) {
	handler, cancel := newTestHandler(t)
	defer cancel()

	req := httptest.NewRequest(http.MethodGet, "/api/slaves/2/setpoints", nil)
	rec := httptest.NewRecorder()
	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Fatalf("status = %d body=%s", rec.Code, rec.Body.String())
	}
}

func TestSlaveSetpointsEndpointInvalidSlaveID(t *testing.T) {
	handler, cancel := newTestHandler(t)
	defer cancel()

	req := httptest.NewRequest(http.MethodGet, "/api/slaves/not-a-number/setpoints", nil)
	rec := httptest.NewRecorder()
	handler.ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Fatalf("status = %d body=%s", rec.Code, rec.Body.String())
	}
}
