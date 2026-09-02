package stats

import (
	"bytes"
	"compress/gzip"
	"context"
	"errors"
	"io"
	"log/slog"
	"strings"
	"testing"
)

type fakeObjects struct {
	objects map[string][]byte
	fail    map[string]error
}

func (f *fakeObjects) List(prefix string) ([]string, error) {
	var keys []string
	for k := range f.objects {
		if strings.HasPrefix(k, prefix) {
			keys = append(keys, k)
		}
	}
	return keys, nil
}

func (f *fakeObjects) Get(key string) (io.ReadCloser, error) {
	if err := f.fail[key]; err != nil {
		return nil, err
	}
	return io.NopCloser(bytes.NewReader(f.objects[key])), nil
}

type fakeApplier struct {
	processed map[string]bool
	applied   map[string]*Rollup
}

func newFakeApplier() *fakeApplier {
	return &fakeApplier{processed: map[string]bool{}, applied: map[string]*Rollup{}}
}

func (f *fakeApplier) Unprocessed(_ context.Context, keys []string) ([]string, error) {
	var out []string
	for _, k := range keys {
		if !f.processed[k] {
			out = append(out, k)
		}
	}
	return out, nil
}

func (f *fakeApplier) ApplyRollup(_ context.Context, key string, rollup *Rollup) error {
	f.processed[key] = true
	f.applied[key] = rollup
	return nil
}

func gzipped(t *testing.T, contents string) []byte {
	t.Helper()
	var buf bytes.Buffer
	w := gzip.NewWriter(&buf)
	if _, err := w.Write([]byte(contents)); err != nil {
		t.Fatal(err)
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

const oneLine = `{"status":200,"request":{"host":"api.1d4.net","method":"GET","uri":"/x","headers":{}}}`

func testAggregator(objects *fakeObjects, store *fakeApplier) *Aggregator {
	return &Aggregator{
		Objects: objects,
		Store:   store,
		Logger:  slog.New(slog.NewTextHandler(io.Discard, nil)),
	}
}

func TestRunOnceAggregatesNewObjectsAndSkipsProcessedAndForeignKeys(t *testing.T) {
	objects := &fakeObjects{objects: map[string][]byte{
		"logs/source=caddy/dt=2026-08-30/a.log.gz": gzipped(t, oneLine),
		"logs/source=caddy/dt=2026-08-31/b.log.gz": gzipped(t, oneLine),
		"logs/source=app/dt=2026-08-30/c.log.gz":   gzipped(t, oneLine),
		"unrelated/readme.txt":                     []byte("x"),
	}}
	store := newFakeApplier()
	store.processed["logs/source=caddy/dt=2026-08-30/a.log.gz"] = true

	processed, err := testAggregator(objects, store).RunOnce(context.Background())

	if err != nil || processed != 1 {
		t.Fatalf("RunOnce = (%d, %v), want only the new caddy object", processed, err)
	}
	rollup := store.applied["logs/source=caddy/dt=2026-08-31/b.log.gz"]
	if rollup == nil {
		t.Fatal("the new object was not applied")
	}
	// The partition date keys the rollup — the object's own dt=, not today.
	if got := rollup.Requests[RequestKey{"2026-08-31", "api.1d4.net", 200, "GET", AgentOther, "(empty)"}]; got != 1 {
		t.Errorf("rollup rows = %v", rollup.Requests)
	}
}

func TestRunOnceReadsOneD4ObjectsAsQueryEvents(t *testing.T) {
	objects := &fakeObjects{objects: map[string][]byte{
		"logs/source=one_d4/dt=2026-09-01/query_events-2026-09-01T14.log.gz": gzipped(t,
			eventLine("entry", "query", "source", "ui", "outcome", "ok", "cache", "live", "duration_us", "100")),
		// A caddy-shaped line under the one_d4 prefix is not an event; the object still applies.
		"logs/source=one_d4/dt=2026-09-01/stray.log.gz": gzipped(t, oneLine),
	}}
	store := newFakeApplier()

	processed, err := testAggregator(objects, store).RunOnce(context.Background())

	if err != nil || processed != 2 {
		t.Fatalf("RunOnce = (%d, %v), want both one_d4 objects", processed, err)
	}
	rollup := store.applied["logs/source=one_d4/dt=2026-09-01/query_events-2026-09-01T14.log.gz"]
	if got := rollup.Queries[QueryKey{"2026-09-01", "query", "ui", "ok", "live"}]; got != (QueryStat{1, 100}) {
		t.Errorf("query rows = %v", rollup.Queries)
	}
	if len(rollup.Requests) != 0 {
		t.Errorf("a one_d4 object minted request rows: %v", rollup.Requests)
	}
	stray := store.applied["logs/source=one_d4/dt=2026-09-01/stray.log.gz"]
	if len(stray.Queries) != 0 || len(stray.Requests) != 0 {
		t.Errorf("the stray object minted rows: %v %v", stray.Queries, stray.Requests)
	}
}

func TestRunOnceLeavesAFailingObjectForTheNextPassAndProcessesTheRest(t *testing.T) {
	objects := &fakeObjects{
		objects: map[string][]byte{
			"logs/source=caddy/dt=2026-08-30/bad.log.gz":  []byte("not gzip"),
			"logs/source=caddy/dt=2026-08-30/good.log.gz": gzipped(t, oneLine),
		},
		fail: map[string]error{},
	}
	store := newFakeApplier()

	processed, err := testAggregator(objects, store).RunOnce(context.Background())

	if err != nil {
		t.Fatalf("a per-object failure must not fail the pass: %v", err)
	}
	if processed != 1 {
		t.Errorf("processed = %d, want the good object", processed)
	}
	if store.processed["logs/source=caddy/dt=2026-08-30/bad.log.gz"] {
		t.Error("the corrupt object was marked processed; it can never be retried or noticed")
	}
}

func TestRunOnceSurfacesAListingFailure(t *testing.T) {
	objects := &fakeObjects{objects: map[string][]byte{}}
	store := newFakeApplier()
	agg := testAggregator(objects, store)
	agg.Objects = failingLister{}

	if _, err := agg.RunOnce(context.Background()); err == nil {
		t.Error("a listing failure is the whole pass failing; it must not read as quiet success")
	}
}

type failingLister struct{}

func (failingLister) List(string) ([]string, error) { return nil, errors.New("s3 down") }
func (failingLister) Get(string) (io.ReadCloser, error) {
	return nil, errors.New("unreachable")
}
