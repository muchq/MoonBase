package stats

import (
	"context"
	"encoding/json"
	"log/slog"
	"net/http"
	"strconv"
)

// Reader is what the HTTP handlers need from the store — an interface so
// the handler tests run against a map instead of a database.
type Reader interface {
	Summary(ctx context.Context, days int) ([]SummaryRow, error)
	TopSlugs(ctx context.Context, days, limit int) ([]SlugRow, error)
	Agents(ctx context.Context, days, limit int) ([]AgentRow, error)
	Probes(ctx context.Context, days int) ([]ProbeRow, error)
}

type Handlers struct {
	reader Reader
	logger *slog.Logger
}

func NewHandlers(reader Reader, logger *slog.Logger) *Handlers {
	return &Handlers{reader: reader, logger: logger}
}

func (h *Handlers) Health(w http.ResponseWriter, _ *http.Request) {
	w.Write([]byte(`{"status":"healthy"}`))
}

// queryInt reads an integer query parameter, clamped to [1, max]; absent
// or unparseable reads as the default rather than an error — a stats page
// with a mangled query string should degrade to the default window, not 400.
func queryInt(r *http.Request, name string, def, max int) int {
	raw := r.URL.Query().Get(name)
	value, err := strconv.Atoi(raw)
	if raw == "" || err != nil {
		return def
	}
	if value < 1 {
		return 1
	}
	if value > max {
		return max
	}
	return value
}

func (h *Handlers) GetSummary(w http.ResponseWriter, r *http.Request) {
	days := queryInt(r, "days", 7, 365)
	rows, err := h.reader.Summary(r.Context(), days)
	if err != nil {
		h.serverError(w, "summary", err)
		return
	}
	writeJSON(w, map[string]any{"days": days, "rows": emptyIfNil(rows)})
}

func (h *Handlers) GetTopSlugs(w http.ResponseWriter, r *http.Request) {
	days := queryInt(r, "days", 30, 365)
	limit := queryInt(r, "limit", 20, 200)
	rows, err := h.reader.TopSlugs(r.Context(), days, limit)
	if err != nil {
		h.serverError(w, "top slugs", err)
		return
	}
	writeJSON(w, map[string]any{"days": days, "rows": emptyIfNil(rows)})
}

func (h *Handlers) GetAgents(w http.ResponseWriter, r *http.Request) {
	days := queryInt(r, "days", 30, 365)
	limit := queryInt(r, "limit", 500, 2000)
	rows, err := h.reader.Agents(r.Context(), days, limit)
	if err != nil {
		h.serverError(w, "agents", err)
		return
	}
	writeJSON(w, map[string]any{"days": days, "rows": emptyIfNil(rows)})
}

func (h *Handlers) GetProbes(w http.ResponseWriter, r *http.Request) {
	days := queryInt(r, "days", 30, 365)
	rows, err := h.reader.Probes(r.Context(), days)
	if err != nil {
		h.serverError(w, "probes", err)
		return
	}
	writeJSON(w, map[string]any{"days": days, "rows": emptyIfNil(rows)})
}

func (h *Handlers) serverError(w http.ResponseWriter, what string, err error) {
	h.logger.Error("stats query failed", "query", what, "error", err)
	// The reason goes to the log, not the wire: these endpoints are public.
	http.Error(w, `{"error":"internal"}`, http.StatusInternalServerError)
}

func writeJSON(w http.ResponseWriter, payload any) {
	json.NewEncoder(w).Encode(payload)
}

// emptyIfNil keeps "no rows yet" serializing as [] rather than null — the
// dashboard maps over it.
func emptyIfNil[T any](rows []T) []T {
	if rows == nil {
		return []T{}
	}
	return rows
}
