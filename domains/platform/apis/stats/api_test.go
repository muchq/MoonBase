package stats

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

type fakeReader struct {
	summary   []SummaryRow
	slugs     []SlugRow
	agents    []AgentRow
	probes    []ProbeRow
	lastDays  int
	lastLimit int
	fail      bool
}

func (f *fakeReader) Summary(_ context.Context, days int) ([]SummaryRow, error) {
	if f.fail {
		return nil, errors.New("db is having a day")
	}
	f.lastDays = days
	return f.summary, nil
}

func (f *fakeReader) TopSlugs(_ context.Context, days, limit int) ([]SlugRow, error) {
	if f.fail {
		return nil, errors.New("db is having a day")
	}
	f.lastDays, f.lastLimit = days, limit
	return f.slugs, nil
}

func (f *fakeReader) Agents(_ context.Context, days, limit int) ([]AgentRow, error) {
	if f.fail {
		return nil, errors.New("db is having a day")
	}
	f.lastDays, f.lastLimit = days, limit
	return f.agents, nil
}

func (f *fakeReader) Probes(_ context.Context, days int) ([]ProbeRow, error) {
	if f.fail {
		return nil, errors.New("db is having a day")
	}
	f.lastDays = days
	return f.probes, nil
}

func handlersWith(reader *fakeReader) *Handlers {
	return NewHandlers(reader, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

func get(t *testing.T, handler http.HandlerFunc, url string) (*httptest.ResponseRecorder, map[string]any) {
	t.Helper()
	recorder := httptest.NewRecorder()
	handler(recorder, httptest.NewRequest("GET", url, nil))
	var body map[string]any
	if recorder.Code == http.StatusOK {
		if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
			t.Fatalf("unparseable response %q: %v", recorder.Body.String(), err)
		}
	}
	return recorder, body
}

func TestSummaryClampsDaysAndReturnsRows(t *testing.T) {
	reader := &fakeReader{summary: []SummaryRow{
		{Date: "2026-08-30", Host: "api.1d4.net", AgentClass: "browser", Requests: 10, Errors: 1},
	}}
	handlers := handlersWith(reader)

	_, body := get(t, handlers.GetSummary, "/stats/v1/summary?days=99999")
	if reader.lastDays != 365 {
		t.Errorf("days clamped to %d, want 365", reader.lastDays)
	}
	if rows := body["rows"].([]any); len(rows) != 1 {
		t.Errorf("rows = %v", body["rows"])
	}

	// Absent and garbage both degrade to the default window, not a 400 — a
	// public stats page with a mangled query string still renders.
	get(t, handlers.GetSummary, "/stats/v1/summary")
	if reader.lastDays != 7 {
		t.Errorf("default days = %d, want 7", reader.lastDays)
	}
	get(t, handlers.GetSummary, "/stats/v1/summary?days=banana")
	if reader.lastDays != 7 {
		t.Errorf("garbage days = %d, want the default 7", reader.lastDays)
	}
}

func TestTopSlugsPassesWindowAndLimit(t *testing.T) {
	reader := &fakeReader{}
	_, body := get(t, handlersWith(reader).GetTopSlugs, "/stats/v1/iili/top?days=30&limit=5")
	if reader.lastDays != 30 || reader.lastLimit != 5 {
		t.Errorf("(days, limit) = (%d, %d)", reader.lastDays, reader.lastLimit)
	}
	// No rows yet must serialize as [], not null — the dashboard maps it.
	if body["rows"] == nil {
		t.Error(`rows serialized as null; want []`)
	}
}

func TestNonPositiveAndOversizedParametersClampRatherThan400(t *testing.T) {
	reader := &fakeReader{}
	handlers := handlersWith(reader)

	for _, raw := range []string{"0", "-5"} {
		if recorder, _ := get(t, handlers.GetSummary, "/stats/v1/summary?days="+raw); recorder.Code != http.StatusOK {
			t.Errorf("days=%s answered %d, want 200", raw, recorder.Code)
		}
		if reader.lastDays != 1 {
			t.Errorf("days=%s clamped to %d, want 1", raw, reader.lastDays)
		}
	}
	get(t, handlers.GetTopSlugs, "/stats/v1/iili/top?limit=99999")
	if reader.lastLimit != 200 {
		t.Errorf("slug limit clamped to %d, want 200", reader.lastLimit)
	}
}

func TestAStoreFailureIs500WithoutTheReasonOnTheWire(t *testing.T) {
	recorder, _ := get(t, handlersWith(&fakeReader{fail: true}).GetSummary, "/stats/v1/summary")
	if recorder.Code != http.StatusInternalServerError {
		t.Errorf("status = %d, want 500", recorder.Code)
	}
	if strings.Contains(recorder.Body.String(), "db is having a day") {
		t.Error("the failure reason leaked to a public endpoint")
	}
}

func TestAgentsAndProbesShareTheWindowRules(t *testing.T) {
	reader := &fakeReader{
		agents: []AgentRow{{Date: "2026-08-30", Host: "git.muchq.com", AgentClass: AgentAIScraper, Agent: "meta-externalagent", Requests: 9, Blocked: 9}},
		probes: []ProbeRow{{Host: "api.muchq.com", Probe: ProbeWordpress, Requests: 4, Served: 0}},
	}
	handlers := handlersWith(reader)

	_, body := get(t, handlers.GetAgents, "/stats/v1/agents")
	if reader.lastDays != 30 || reader.lastLimit != 500 {
		t.Errorf("default agents (days, limit) = (%d, %d), want (30, 500)", reader.lastDays, reader.lastLimit)
	}
	row := body["rows"].([]any)[0].(map[string]any)
	if row["agent"] != "meta-externalagent" || row["agent_class"] != AgentAIScraper || row["blocked"] != float64(9) {
		t.Errorf("agent row = %v", row)
	}

	get(t, handlers.GetAgents, "/stats/v1/agents?days=99999&limit=99999")
	if reader.lastDays != 365 || reader.lastLimit != 2000 {
		t.Errorf("clamped agents (days, limit) = (%d, %d), want (365, 2000)", reader.lastDays, reader.lastLimit)
	}

	get(t, handlers.GetProbes, "/stats/v1/probes")
	if reader.lastDays != 30 {
		t.Errorf("default probes window = %d, want 30", reader.lastDays)
	}
	_, body = get(t, handlers.GetProbes, "/stats/v1/probes?days=99999")
	if reader.lastDays != 365 {
		t.Errorf("probe window clamped to %d, want 365", reader.lastDays)
	}
	row = body["rows"].([]any)[0].(map[string]any)
	if row["probe"] != ProbeWordpress || row["served"] != float64(0) {
		t.Errorf("probe row = %v", row)
	}

	// Empty is [], not null, on both — the dashboard maps them.
	_, body = get(t, handlersWith(&fakeReader{}).GetAgents, "/stats/v1/agents")
	if body["rows"] == nil {
		t.Error("agents rows serialized as null; want []")
	}
	_, body = get(t, handlersWith(&fakeReader{}).GetProbes, "/stats/v1/probes")
	if body["rows"] == nil {
		t.Error("probes rows serialized as null; want []")
	}
	failing := handlersWith(&fakeReader{fail: true})
	for name, handler := range map[string]http.HandlerFunc{"agents": failing.GetAgents, "probes": failing.GetProbes} {
		if recorder, _ := get(t, handler, "/x"); recorder.Code != http.StatusInternalServerError {
			t.Errorf("%s on a failing store = %d, want 500", name, recorder.Code)
		}
	}
}
