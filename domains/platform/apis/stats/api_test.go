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
	summary      []SummaryRow
	services     []ServiceRow
	serviceTotal int
	slugs        []SlugRow
	agents       []AgentRow
	probes       []ProbeRow
	queries      []QueryRow
	hubEvents    []HubEventRow
	terms        []TermRow
	termTotal    int
	countries    []CountryRow
	lastDays     int
	lastLimit    int
	fail         bool
}

func (f *fakeReader) Summary(_ context.Context, days int) ([]SummaryRow, error) {
	if f.fail {
		return nil, errors.New("db is having a day")
	}
	f.lastDays = days
	return f.summary, nil
}

func (f *fakeReader) Services(_ context.Context, days, limit int) ([]ServiceRow, int, error) {
	if f.fail {
		return nil, 0, errors.New("db is having a day")
	}
	f.lastDays, f.lastLimit = days, limit
	return f.services, f.serviceTotal, nil
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

func (f *fakeReader) Queries(_ context.Context, days int) ([]QueryRow, error) {
	if f.fail {
		return nil, errors.New("db is having a day")
	}
	f.lastDays = days
	return f.queries, nil
}

func (f *fakeReader) HubEvents(_ context.Context, days int) ([]HubEventRow, error) {
	if f.fail {
		return nil, errors.New("db is having a day")
	}
	f.lastDays = days
	return f.hubEvents, nil
}

func (f *fakeReader) QueryTerms(_ context.Context, days, limit int) ([]TermRow, int, error) {
	if f.fail {
		return nil, 0, errors.New("db is having a day")
	}
	f.lastDays, f.lastLimit = days, limit
	return f.terms, f.termTotal, nil
}

func (f *fakeReader) Countries(_ context.Context, days, limit int) ([]CountryRow, error) {
	if f.fail {
		return nil, errors.New("db is having a day")
	}
	f.lastDays, f.lastLimit = days, limit
	return f.countries, nil
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

func TestServicesPassesWindowAndLimitAndNamesTheBackend(t *testing.T) {
	reader := &fakeReader{serviceTotal: 42, services: []ServiceRow{
		{Date: "2026-08-30", Host: "api.muchq.com", Service: "one_d4",
			Source: SourceUI, AgentClass: AgentBrowser, Requests: 9, Errors: 1},
	}}
	_, body := get(t, handlersWith(reader).GetServices, "/stats/v1/services?days=30&limit=5")
	if reader.lastDays != 30 || reader.lastLimit != 5 {
		t.Errorf("(days, limit) = (%d, %d)", reader.lastDays, reader.lastLimit)
	}
	rows := body["rows"].([]any)
	if len(rows) != 1 {
		t.Fatalf("rows = %v", body["rows"])
	}
	// The page groups on these three, so they are part of the response
	// shape and not incidental.
	row := rows[0].(map[string]any)
	for field, want := range map[string]any{
		"service": "one_d4", "source": SourceUI, "agent_class": AgentBrowser,
	} {
		if row[field] != want {
			t.Errorf("row[%q] = %v, want %v", field, row[field], want)
		}
	}
	// And the count before the limit, so a reader summing a truncated
	// list can tell that it got one.
	if body["total"] != float64(42) {
		t.Errorf("total = %v, want 42", body["total"])
	}
}

// The window rules, on the same terms as every neighbour: a default
// window, a ceiling well below what an uncompressed public response can
// afford, [] rather than null for a reader that maps the rows, and a
// store failure that is a 500 and not a 200 holding nothing.
func TestServicesSharesTheWindowRules(t *testing.T) {
	reader := &fakeReader{}
	get(t, handlersWith(reader).GetServices, "/stats/v1/services")
	if reader.lastDays != 7 || reader.lastLimit != 2000 {
		t.Errorf("default services (days, limit) = (%d, %d), want (7, 2000)",
			reader.lastDays, reader.lastLimit)
	}
	get(t, handlersWith(reader).GetServices, "/stats/v1/services?limit=99999")
	if reader.lastLimit != 5000 {
		t.Errorf("limit clamped to %d, want 5000", reader.lastLimit)
	}
	if _, body := get(t, handlersWith(&fakeReader{}).GetServices, "/stats/v1/services"); body["rows"] == nil {
		t.Error("services rows serialized as null; want []")
	}
	recorder, _ := get(t, handlersWith(&fakeReader{fail: true}).GetServices, "/stats/v1/services")
	if recorder.Code != http.StatusInternalServerError {
		t.Errorf("services on a failing store = %d, want 500", recorder.Code)
	}
	if strings.Contains(recorder.Body.String(), "db is having a day") {
		t.Error("the failure reason leaked to a public endpoint")
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

func TestOneD4QueryEndpointsShareTheWindowRules(t *testing.T) {
	reader := &fakeReader{
		queries:   []QueryRow{{Date: "2026-09-01", Entry: "query", Source: "ui", Outcome: "ok", Cache: "live", Requests: 4}},
		terms:     []TermRow{{Kind: KindField, Term: "white.elo", Requests: 9}},
		termTotal: 42,
	}
	handlers := handlersWith(reader)

	_, body := get(t, handlers.GetQueries, "/stats/v1/one_d4/queries")
	if reader.lastDays != 30 {
		t.Errorf("default queries window = %d, want 30", reader.lastDays)
	}
	row := body["rows"].([]any)[0].(map[string]any)
	if row["cache"] != "live" || row["requests"] != float64(4) {
		t.Errorf("query row = %v", row)
	}

	_, body = get(t, handlers.GetQueryTerms, "/stats/v1/one_d4/terms")
	if reader.lastDays != 30 || reader.lastLimit != 200 {
		t.Errorf("default terms (days, limit) = (%d, %d), want (30, 200)", reader.lastDays, reader.lastLimit)
	}
	row = body["rows"].([]any)[0].(map[string]any)
	if row["kind"] != "field" || row["term"] != "white.elo" {
		t.Errorf("term row = %v", row)
	}
	// The count before the limit, so a reader can tell a whole window from
	// a truncated one. The rows themselves are complete totals either way.
	if body["total"] != float64(42) {
		t.Errorf("terms total = %v, want the reader's 42", body["total"])
	}
	if row["entry"] != nil {
		t.Errorf("term row still carries an entry: %v", row)
	}
	get(t, handlers.GetQueryTerms, "/stats/v1/one_d4/terms?days=99999&limit=99999")
	if reader.lastDays != 365 || reader.lastLimit != 1000 {
		t.Errorf("clamped terms (days, limit) = (%d, %d), want (365, 1000)", reader.lastDays, reader.lastLimit)
	}

	for name, handler := range map[string]http.HandlerFunc{"queries": handlersWith(&fakeReader{}).GetQueries, "terms": handlersWith(&fakeReader{}).GetQueryTerms} {
		if _, body := get(t, handler, "/x"); body["rows"] == nil {
			t.Errorf("%s rows serialized as null; want []", name)
		}
	}
	failing := handlersWith(&fakeReader{fail: true})
	for name, handler := range map[string]http.HandlerFunc{"queries": failing.GetQueries, "terms": failing.GetQueryTerms} {
		if recorder, _ := get(t, handler, "/x"); recorder.Code != http.StatusInternalServerError {
			t.Errorf("%s on a failing store = %d, want 500", name, recorder.Code)
		}
	}
}

func TestHubEventsSharesTheWindowRules(t *testing.T) {
	reader := &fakeReader{hubEvents: []HubEventRow{
		{Date: "2026-09-21", Event: "game_finished", Variant: "golf", Outcome: "abandoned",
			Players: 1, Events: 3},
	}}
	handlers := handlersWith(reader)

	_, body := get(t, handlers.GetHubEvents, "/stats/v1/games_hub/events")
	if reader.lastDays != 30 {
		t.Errorf("default hub events window = %d, want 30", reader.lastDays)
	}
	// The page groups on these, so they are the response shape and not
	// incidental — including the empty surface a finished game has none of.
	row := body["rows"].([]any)[0].(map[string]any)
	for field, want := range map[string]any{
		"event": "game_finished", "variant": "golf", "outcome": "abandoned",
		"surface": "", "players": float64(1), "events": float64(3),
	} {
		if row[field] != want {
			t.Errorf("row[%q] = %v, want %v", field, row[field], want)
		}
	}

	get(t, handlers.GetHubEvents, "/stats/v1/games_hub/events?days=99999")
	if reader.lastDays != 365 {
		t.Errorf("hub events days clamped to %d, want 365", reader.lastDays)
	}
	if _, body := get(t, handlersWith(&fakeReader{}).GetHubEvents, "/x"); body["rows"] == nil {
		t.Error("hub event rows serialized as null; want []")
	}
	recorder, _ := get(t, handlersWith(&fakeReader{fail: true}).GetHubEvents, "/x")
	if recorder.Code != http.StatusInternalServerError {
		t.Errorf("hub events on a failing store = %d, want 500", recorder.Code)
	}
	if strings.Contains(recorder.Body.String(), "db is having a day") {
		t.Error("the failure reason leaked to a public endpoint")
	}
}

func TestCountriesShareTheWindowRules(t *testing.T) {
	reader := &fakeReader{countries: []CountryRow{
		{Host: "git.muchq.com", AgentClass: AgentAIScraper, Country: "US", Requests: 9, Blocked: 4, Probes: 2},
	}}
	handlers := handlersWith(reader)

	_, body := get(t, handlers.GetCountries, "/stats/v1/countries")
	if reader.lastDays != 30 || reader.lastLimit != 2000 {
		t.Errorf("default countries (days, limit) = (%d, %d), want (30, 2000)", reader.lastDays, reader.lastLimit)
	}
	row := body["rows"].([]any)[0].(map[string]any)
	if row["country"] != "US" || row["requests"] != float64(9) || row["blocked"] != float64(4) || row["probes"] != float64(2) {
		t.Errorf("country row = %v", row)
	}
	get(t, handlers.GetCountries, "/stats/v1/countries?days=99999&limit=99999")
	if reader.lastDays != 365 || reader.lastLimit != 5000 {
		t.Errorf("clamped countries (days, limit) = (%d, %d), want (365, 5000)", reader.lastDays, reader.lastLimit)
	}
	if _, body := get(t, handlersWith(&fakeReader{}).GetCountries, "/x"); body["rows"] == nil {
		t.Error("countries rows serialized as null; want []")
	}
	if recorder, _ := get(t, handlersWith(&fakeReader{fail: true}).GetCountries, "/x"); recorder.Code != http.StatusInternalServerError {
		t.Errorf("countries on a failing store = %d, want 500", recorder.Code)
	}
}
