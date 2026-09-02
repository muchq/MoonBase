package stats

import (
	"compress/gzip"
	"context"
	"fmt"
	"io"
	"log/slog"
	"regexp"
	"sort"
	"strings"
)

// ObjectStore is the slice of s3lite the aggregation loop needs, an
// interface so the loop tests run against maps.
type ObjectStore interface {
	List(prefix string) ([]string, error)
	Get(key string) (io.ReadCloser, error)
}

// Applier is the write half of the store, split from Reader so the loop
// tests record applications without a database.
type Applier interface {
	Unprocessed(ctx context.Context, keys []string) ([]string, error)
	ApplyRollup(ctx context.Context, key string, rollup *Rollup) error
}

// The shipper's layout: logs/source=<source>/dt=YYYY-MM-DD/<file>. The
// source names its parser in sources; the date is the partition the
// rollup is keyed by.
var objectKey = regexp.MustCompile(`^logs/source=([a-z0-9_]+)/dt=(\d{4}-\d{2}-\d{2})/`)

// The sources aggregated, each under its own prefix, and how each is
// read: Caddy's access logs and one_d4's query events (#1465). Adding a
// source is one entry here and nothing else.
var sources = map[string]func(*Rollup, io.Reader, string) (int, error){
	"caddy":  (*Rollup).Consume,
	"one_d4": (*Rollup).ConsumeQueryEvents,
}

func sourcePrefixes() []string {
	var prefixes []string
	for source := range sources {
		prefixes = append(prefixes, "logs/source="+source+"/")
	}
	sort.Strings(prefixes)
	return prefixes
}

type Aggregator struct {
	Objects ObjectStore
	Store   Applier
	Logger  *slog.Logger
	// Geo places client addresses for the geo rollup; nil means none loaded.
	Geo Locator
}

// RunOnce aggregates every not-yet-processed object under the source
// prefixes. Per-object failures are logged and skipped — the object stays
// unprocessed and the next pass retries it — so one corrupt or half-
// shipped object cannot wedge the loop.
func (a *Aggregator) RunOnce(ctx context.Context) (processed int, err error) {
	var candidates []string
	for _, prefix := range sourcePrefixes() {
		keys, err := a.Objects.List(prefix)
		if err != nil {
			return 0, fmt.Errorf("listing log objects under %s: %w", prefix, err)
		}
		for _, key := range keys {
			if objectKey.MatchString(key) {
				candidates = append(candidates, key)
			}
		}
	}
	if len(candidates) == 0 {
		return 0, nil
	}
	pending, err := a.Store.Unprocessed(ctx, candidates)
	if err != nil {
		return 0, fmt.Errorf("checking processed markers: %w", err)
	}
	for _, key := range pending {
		if err := a.processObject(ctx, key); err != nil {
			a.Logger.Error("aggregating object failed; will retry next pass",
				"key", key, "error", err)
			continue
		}
		processed++
	}
	return processed, nil
}

func (a *Aggregator) processObject(ctx context.Context, key string) error {
	match := objectKey.FindStringSubmatch(key)
	source, date := match[1], match[2]
	body, err := a.Objects.Get(key)
	if err != nil {
		return err
	}
	defer body.Close()

	var reader io.Reader = body
	if strings.HasSuffix(key, ".gz") {
		gz, err := gzip.NewReader(body)
		if err != nil {
			return fmt.Errorf("not gzip: %w", err)
		}
		defer gz.Close()
		reader = gz
	}

	consume, ok := sources[source]
	if !ok {
		return fmt.Errorf("no parser for source %q", source)
	}
	rollup := NewRollup()
	if a.Geo != nil {
		rollup.Geo = a.Geo
	}
	skipped, err := consume(rollup, reader, date)
	if err != nil {
		return err
	}
	if skipped > 0 {
		a.Logger.Warn("object had unparseable lines", "key", key, "skipped", skipped)
	}
	return a.Store.ApplyRollup(ctx, key, rollup)
}
