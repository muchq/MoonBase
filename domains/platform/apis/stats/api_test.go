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

func TestAStoreFailureIs500WithoutTheReasonOnTheWire(t *testing.T) {
	recorder, _ := get(t, handlersWith(&fakeReader{fail: true}).GetSummary, "/stats/v1/summary")
	if recorder.Code != http.StatusInternalServerError {
		t.Errorf("status = %d, want 500", recorder.Code)
	}
	if strings.Contains(recorder.Body.String(), "db is having a day") {
		t.Error("the failure reason leaked to a public endpoint")
	}
}
