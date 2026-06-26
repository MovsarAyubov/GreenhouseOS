package api

import (
	"encoding/json"
	"net/http"
	"strconv"

	"greenhouse-hub/internal/hub"
)

type Handler struct {
	service *hub.Service
	mux     *http.ServeMux
}

func NewHandler(service *hub.Service) http.Handler {
	h := &Handler{service: service, mux: http.NewServeMux()}
	h.routes()
	return h
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
	w.Header().Set("Access-Control-Allow-Methods", "GET,POST,OPTIONS")
	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	h.mux.ServeHTTP(w, r)
}

func (h *Handler) routes() {
	h.mux.HandleFunc("GET /api/health", h.health)
	h.mux.HandleFunc("GET /api/state", h.state)
	h.mux.HandleFunc("GET /api/slaves", h.slaves)
	h.mux.HandleFunc("GET /api/slaves/{slave_id}/setpoints", h.slaveSetpoints)
	h.mux.HandleFunc("GET /api/slave-maps", h.slaveMaps)
	h.mux.HandleFunc("GET /api/points", h.points)
	h.mux.HandleFunc("POST /api/setpoints", h.setpoints)
	h.mux.HandleFunc("POST /api/scan", h.scan)
	h.mux.HandleFunc("GET /api/scan", h.lastScan)
}

func (h *Handler) health(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (h *Handler) state(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, h.service.Snapshot())
}

func (h *Handler) slaves(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, h.service.Snapshot().Slaves)
}

func (h *Handler) slaveSetpoints(w http.ResponseWriter, r *http.Request) {
	slaveID, ok := parsePathUint8(r.PathValue("slave_id"))
	if !ok || slaveID == 0 {
		writeError(w, http.StatusBadRequest, "invalid slave_id")
		return
	}
	includeUnsupported := parseBoolQuery(r.URL.Query().Get("include_unsupported"))
	catalog, err := h.service.SetpointsForSlave(r.Context(), slaveID, includeUnsupported)
	if err != nil {
		writeError(w, http.StatusNotFound, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, catalog)
}

func (h *Handler) slaveMaps(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, h.service.SlaveMaps())
}

func (h *Handler) points(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, h.service.Snapshot().Points)
}

func (h *Handler) setpoints(w http.ResponseWriter, r *http.Request) {
	var req hub.SetpointRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	result := h.service.ApplySetpoint(r.Context(), req)
	if !result.Applied {
		writeJSON(w, http.StatusBadRequest, result)
		return
	}
	writeJSON(w, http.StatusAccepted, result)
}

func (h *Handler) scan(w http.ResponseWriter, r *http.Request) {
	from := parseUint8(r.URL.Query().Get("from"))
	to := parseUint8(r.URL.Query().Get("to"))
	writeJSON(w, http.StatusOK, h.service.Scan(r.Context(), from, to))
}

func (h *Handler) lastScan(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, h.service.LastScan())
}

func parseUint8(value string) uint8 {
	if value == "" {
		return 0
	}
	parsed, err := strconv.ParseUint(value, 10, 8)
	if err != nil {
		return 0
	}
	return uint8(parsed)
}

func parsePathUint8(value string) (uint8, bool) {
	parsed, err := strconv.ParseUint(value, 10, 8)
	if err != nil {
		return 0, false
	}
	return uint8(parsed), true
}

func parseBoolQuery(value string) bool {
	switch value {
	case "1", "true", "TRUE", "yes", "YES", "on", "ON":
		return true
	default:
		return false
	}
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(payload)
}

func writeError(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, map[string]string{"error": message})
}
