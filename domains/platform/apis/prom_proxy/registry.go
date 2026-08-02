package prom_proxy

import "fmt"

// The service registry behind the dashboard overhaul (#1199): the catalog
// endpoint, the standard http_server_* block, and each service's custom
// metric descriptors all read from here — adding an emitter is one entry,
// no new routes, handlers, or UI code.

// MetricView is which form a counter-derived tile is asked for (#1287).
//
// Two forms, not three. A counter can also be read cumulatively as sum(x),
// which is what most of these tiles did before, but that is deliberately not
// offered: a cumulative counter resets when its process does, so it reads low
// after every deploy and cannot be compared across one. increase() is the
// restart-corrected form and answers the same question.
type MetricView string

const (
	// ViewCount is the number of events in the window: sum(increase(x[w])).
	ViewCount MetricView = "count"
	// ViewRate is events per second: sum(rate(x[w])).
	ViewRate MetricView = "rate"
)

// DefaultView is what a request that names no view gets. Count, because most
// of these tiles read as counts today and "N in the last five minutes" is the
// more direct answer for an outcome counter; rate is one parameter away.
const DefaultView = ViewCount

// The window both views are computed over, unless a descriptor overrides it.
const defaultCounterWindow = "5m"

// The window for counters that are alarms rather than volumes.
//
// Some of these tiles answer "has this happened at all" — the comment on
// runs_interrupted below says as much: anything above zero means a worker was
// stuck long enough to be given up on. Over five minutes an interruption from
// an hour ago reads zero and the alarm silently disarms, which is the failure
// mode the tile exists to prevent. A day is long enough that a human looking
// at the dashboard after the fact still sees it.
const alarmWindow = "24h"

// ValidView reports whether a client-supplied view is one this package builds
// queries for. Callers must check before passing the value on: a view string
// never reaches PromQL, but an unrecognised one would silently fall through to
// the default and answer a different question than the one asked.
func ValidView(view string) bool {
	switch MetricView(view) {
	case ViewCount, ViewRate:
		return true
	default:
		return false
	}
}

type customScalarDef struct {
	Group string
	Label string
	Unit  string

	// A tile with one fixed form — a gauge, a percentage, a windowed mean —
	// carries its whole expression here and ignores the view.
	Query string

	// A counter-derived tile sets this instead: the bare selector both views
	// are built from, e.g. `games_indexed_total{service_name="one_d4"}`. The
	// proxy wraps it rather than the descriptor spelling out two expressions,
	// so the two forms cannot drift into describing different series.
	Counter string

	// Overrides defaultCounterWindow for this tile. Ignored unless Counter is
	// set.
	Window string
}

// counter declares a toggleable tile over defaultCounterWindow.
//
// The label deliberately carries no _total or _per_sec suffix: the tile shows
// whichever form was asked for, so a suffix naming one of them is wrong half
// the time. The unit says which one is on screen.
func counter(group, label, unit, selector string) customScalarDef {
	return customScalarDef{Group: group, Label: label, Unit: unit, Counter: selector}
}

// counterOver is counter with an explicit window.
func counterOver(group, label, unit, selector, window string) customScalarDef {
	return customScalarDef{Group: group, Label: label, Unit: unit, Counter: selector, Window: window}
}

// scalar declares a tile with a single fixed form and no toggle.
func scalar(group, label, unit, query string) customScalarDef {
	return customScalarDef{Group: group, Label: label, Unit: unit, Query: query}
}

// Toggleable reports whether this tile has both a count and a rate form.
func (d customScalarDef) Toggleable() bool { return d.Counter != "" }

func (d customScalarDef) window() string {
	if d.Window != "" {
		return d.Window
	}
	return defaultCounterWindow
}

// QueryFor builds the expression for one view. A non-toggleable tile answers
// with its fixed query whatever it is asked for.
func (d customScalarDef) QueryFor(view MetricView) string {
	if !d.Toggleable() {
		return d.Query
	}
	fn := "increase"
	if view == ViewRate {
		fn = "rate"
	}
	return fmt.Sprintf("sum(%s(%s[%s]))", fn, d.Counter, d.window())
}

// AllQueries is every expression this descriptor can produce.
//
// Exists for the registry audits: they scan queries for unscoped selectors and
// dead metric names, and a counter tile's Query field is empty, so scanning
// that field alone would let every toggleable tile through unexamined. The
// selectors those tiles are built from are exactly the ones worth checking.
func (d customScalarDef) AllQueries() []string {
	if !d.Toggleable() {
		return []string{d.Query}
	}
	return []string{d.QueryFor(ViewCount), d.QueryFor(ViewRate)}
}

// UnitFor is the unit as displayed in that view. A rate is the tile's own unit
// per second, or a bare "/s" when the quantity is unitless.
func (d customScalarDef) UnitFor(view MetricView) string {
	if !d.Toggleable() || view != ViewRate {
		return d.Unit
	}
	return d.Unit + "/s"
}

type serviceEntry struct {
	CustomScalars    []customScalarDef
	CustomTimeseries map[string]string
}

// Portrait's render cache emits the standard cache family (#1209) from
// aura::Cache rather than bespoke trace_cache_* counters, so its queries
// select on both labels: which service, and which cache within it. Named
// here because every cache query below is built from the same two rates.
//
// These stay in portrait's custom block for now: generalizing them into a
// standard cache block parameterized by service_name — the way the
// http_server_* block already works — is the prom_proxy half of #1209, and
// wants a second emitter to generalize against.
const (
	portraitCacheHitRate  = `rate(cache_hits_total{service_name="portrait",cache="trace"}[5m])`
	portraitCacheMissRate = `rate(cache_misses_total{service_name="portrait",cache="trace"}[5m])`

	portraitCacheHitPercent = portraitCacheHitRate + `/(` + portraitCacheHitRate + `+` +
		portraitCacheMissRate + `)*100`

	// Both cache counters as one selector, so the operations tile can be built
	// in either view from a single expression. Selected by __name__ rather
	// than summed as rate(hits)+rate(misses): that form is two instant vectors
	// joined by binary matching, which yields nothing at all if one side has
	// no series yet — a fresh process whose cache has only ever missed.
	portraitCacheOps = `{__name__=~"cache_hits_total|cache_misses_total",` +
		`service_name="portrait",cache="trace"}`
)

// Scene complexity is recorded on both cache paths and labelled by cache_hit
// (#1287). Summing across the label is offered load — what callers asked to
// render; selecting cache_hit="false" is render cost — what the tracer drew.
// The panel used to show only the second under a name that read like the
// first.
//
// Series recorded before that change carry no cache_hit label at all, so they
// contribute to the requested figures and are invisible to the rendered ones
// until they age out of the window.
const (
	portraitSpheresRequested = `sum(rate(scene_sphere_count_sum[1h]))/` +
		`sum(rate(scene_sphere_count_count[1h]))`
	portraitSpheresRendered = `sum(rate(scene_sphere_count_sum{cache_hit="false"}[1h]))/` +
		`sum(rate(scene_sphere_count_count{cache_hit="false"}[1h]))`
	portraitLightsRequested = `sum(rate(scene_light_count_sum[1h]))/` +
		`sum(rate(scene_light_count_count[1h]))`
	portraitLightsRendered = `sum(rate(scene_light_count_sum{cache_hit="false"}[1h]))/` +
		`sum(rate(scene_light_count_count{cache_hit="false"}[1h]))`
)

// Catalog order doubles as the UI's tab order.
var serviceOrder = []string{"golf_hub", "mcpserver", "microgpt-serve", "mithril", "one_d4", "portrait", "posterize"}

var serviceRegistry = map[string]serviceEntry{
	"golf_hub": {
		CustomScalars: []customScalarDef{
			scalar("Sessions", "active", "sessions", `sum(stream_sessions_active_gauge)`),
			counter("Sessions", "started", "", `stream_sessions_total`),
			counter("Sessions", "resumed", "", `stream_sessions_total{resumed="true"}`),
			counter("Sessions", "refused", "", `stream_admissions_refused_total`),
			counter("Sessions", "disconnects", "", `stream_disconnects_total`),
			counter("Sessions", "seats_expired", "", `stream_seats_expired_total`),
			counter("Activity", "commands", "", `stream_commands_total`),
			counter("Activity", "events", "", `stream_events_total`),
			counter("Activity", "rejections", "", `stream_rejections_total`),
			counter("Activity", "rate_limited", "", `stream_rate_limited_total`),
			// Room chat (#1226): outcome counts and stages only — the emitter
			// never labels by room, player, or text. catch_up_rows is a
			// per-drain distribution, so its windowed average is how far
			// behind a wake found an instance (the lag signal), which is a
			// mean rather than a count and so has no rate form.
			counter("Chat", "messages", "", `chat_appends_total{result="stored"}`),
			counter("Chat", "delivered_rows", "rows", `chat_rows_delivered_total`),
			scalar("Chat", "catch_up_rows_avg_5m", "rows",
				`sum(rate(chat_catch_up_rows_sum[5m]))/sum(rate(chat_catch_up_rows_count[5m]))`),
			counter("Chat", "history_replays", "", `chat_history_replays_total`),
			counterOver("Chat", "failures", "", `chat_failures_total`, alarmWindow),
		},
		CustomTimeseries: map[string]string{
			"sessions_active":    `sum(stream_sessions_active_gauge)`,
			"session_starts":     `sum(increase(stream_sessions_total[5m]))`,
			"command_rate":       `sum(rate(stream_commands_total[5m]))`,
			"event_rate":         `sum(rate(stream_events_total[5m]))`,
			"rejection_rate":     `sum(rate(stream_rejections_total[5m]))`,
			"disconnect_rate":    `sum(rate(stream_disconnects_total[5m]))`,
			"rate_limited_rate":  `sum(rate(stream_rate_limited_total[5m]))`,
			"chat_message_rate":  `sum(rate(chat_appends_total{result="stored"}[5m]))`,
			"chat_delivery_rate": `sum(rate(chat_rows_delivered_total[5m]))`,
			"chat_failure_rate":  `sum(rate(chat_failures_total[5m]))`,
			"chat_catch_up_rows": `sum(rate(chat_catch_up_rows_sum[5m]))/sum(rate(chat_catch_up_rows_count[5m]))`,
		},
	},
	"microgpt-serve": {
		CustomScalars: []customScalarDef{
			counter("Requests by endpoint", "generate", "", `microgpt_requests_total{endpoint="generate"}`),
			counter("Requests by endpoint", "chat", "", `microgpt_requests_total{endpoint="chat"}`),
			counter("Inference", "tokens_generated", "tokens", `microgpt_tokens_generated_total`),
			scalar("Inference", "avg_duration_ms", "ms",
				`sum(rate(microgpt_request_duration_ms_sum[5m]))/sum(rate(microgpt_request_duration_ms_count[5m]))`),
			counter("Inference", "conversations", "", `microgpt_conversations_total`),
		},
		CustomTimeseries: map[string]string{
			"tokens_per_second": `sum(rate(microgpt_tokens_generated_total[5m]))`,
			"avg_duration_ms":   `sum(rate(microgpt_request_duration_ms_sum[5m]))/sum(rate(microgpt_request_duration_ms_count[5m]))`,
		},
	},
	// The Java services (#1212): yodel's standard instruments only, no
	// custom set yet.
	"mcpserver": {},
	// The first Java service with a custom set, now that yodel has counters and
	// distributions to record into. Counts, outcomes and motif names only — the
	// emitter never labels by player or by game, so no series here is per-user.
	"one_d4": {
		CustomScalars: []customScalarDef{
			counter("Indexing", "games_indexed", "games", `games_indexed_total{service_name="one_d4"}`),
			counter("Indexing", "runs_completed", "", `index_runs_total{service_name="one_d4",outcome="completed"}`),
			// The three outcomes below are alarms rather than volumes, so they
			// count over a day. A failed run an hour ago is still the thing an
			// operator opened this page to find.
			counterOver("Indexing", "runs_failed", "",
				`index_runs_total{service_name="one_d4",outcome="failed"}`, alarmWindow),
			// A wedge cut loose by the MAX_RUN ceiling lands here (#1282). Anything
			// above zero means a worker was stuck long enough to be given up on.
			counterOver("Indexing", "runs_interrupted", "",
				`index_runs_total{service_name="one_d4",outcome="interrupted"}`, alarmWindow),
			// Emitted since the ceiling landed, and listed so the four outcomes add up to
			// index_runs_total. A range changing hands mid-run is ordinary; a rising count
			// beside a flat interrupted count is contention, not a wedge — which only
			// reads that way if the two share a window, hence the same one.
			counterOver("Indexing", "runs_lease_lost", "",
				`index_runs_total{service_name="one_d4",outcome="lease_lost"}`, alarmWindow),
			// Windowed averages over the histograms: rate(sum)/rate(count) is the
			// mean per run in the window, the same shape portrait uses. Means, so
			// no rate form.
			// Completed runs only. The histogram is labelled by outcome, and an interrupted
			// run is one that sat at the MAX_RUN ceiling — pooling those in makes the average
			// spike at exactly the moment someone is reading it to size a real run.
			scalar("Indexing", "avg_run_seconds_1h", "s",
				`sum(rate(index_run_duration_micros_sum{service_name="one_d4",outcome="completed"}[1h]))/sum(rate(index_run_duration_micros_count{service_name="one_d4",outcome="completed"}[1h]))/1000000`),
			scalar("Indexing", "avg_games_per_month_1h", "games",
				`sum(rate(index_games_per_month_sum{service_name="one_d4"}[1h]))/sum(rate(index_games_per_month_count{service_name="one_d4"}[1h]))`),
			counter("Indexing", "empty_months", "", `index_months_total{service_name="one_d4",result="empty"}`),
			counter("Indexing", "cached_months", "", `index_months_total{service_name="one_d4",result="cached"}`),
			counter("Indexing", "archive_fetches", "", `chess_com_archive_fetches_total{service_name="one_d4"}`),
			counter("Motifs", "occurrences", "", `motif_occurrences_total{service_name="one_d4"}`),
			// No motifs-per-game tile. The two counters are recorded on opposite sides of
			// the durability boundary — motifs per game inside the drain loop, games only
			// after the month's flush and period write succeed — so an interrupted or
			// lease-lost month contributes motifs and zero games. The ratio then divides
			// by a rate of zero, and any clamp large enough to avoid a divide-by-zero is
			// small enough to turn the result into a four-order-of-magnitude spike. A tile
			// that is wrong exactly when the system is unhealthy is worse than no tile.
		},
		CustomTimeseries: map[string]string{
			"games_indexed_rate":  `sum(rate(games_indexed_total{service_name="one_d4"}[5m]))`,
			"run_completion_rate": `sum(rate(index_runs_total{service_name="one_d4",outcome="completed"}[5m]))`,
			// Selected, not subtracted. In PromQL sum() over no matching series is an
			// empty vector, and vector-minus-empty is empty rather than the left side —
			// so a total-minus-completed form renders nothing on a fresh process whose
			// runs are all failing, which is precisely when someone is looking at it.
			//
			// Named outcomes rather than != "completed": lease_lost is the fourth, and it
			// is ordinary. A range changing hands mid-run happens whenever two pollers
			// overlap, so counting it here puts a permanent floor under the failure line
			// and buries the two outcomes that do mean something went wrong.
			"run_failure_rate":    `sum(rate(index_runs_total{service_name="one_d4",outcome=~"failed|interrupted"}[5m]))`,
			"run_duration_avg_us": `sum(rate(index_run_duration_micros_sum{service_name="one_d4",outcome="completed"}[5m]))/sum(rate(index_run_duration_micros_count{service_name="one_d4",outcome="completed"}[5m]))`,
			"motif_rate":          `sum(rate(motif_occurrences_total{service_name="one_d4"}[5m]))`,
		},
	},
	// Wordchains: server_pal's standard instruments only, no custom set.
	"mithril": {},
	// Image blur/edges: server_pal's standard instruments only.
	"posterize": {},
	"portrait": {
		CustomScalars: []customScalarDef{
			scalar("Render cache", "hit_rate_percent", "%", portraitCacheHitPercent),
			counter("Render cache", "operations", "", portraitCacheOps),
			// Windowed averages over RecordDistribution histograms:
			// rate(sum)/rate(count) = mean per observation in the window.
			// Requested is every accepted request; rendered is the cache
			// misses the tracer actually drew.
			scalar("Scene complexity", "avg_spheres_requested_1h", "spheres", portraitSpheresRequested),
			scalar("Scene complexity", "avg_spheres_rendered_1h", "spheres", portraitSpheresRendered),
			scalar("Scene complexity", "avg_lights_requested_1h", "lights", portraitLightsRequested),
			scalar("Scene complexity", "avg_lights_rendered_1h", "lights", portraitLightsRendered),
		},
		CustomTimeseries: map[string]string{
			"cache_hit_rate":          portraitCacheHitPercent,
			"cache_operations_rate":   `sum(rate(` + portraitCacheOps + `[5m]))`,
			"scene_spheres_requested": `sum(rate(scene_sphere_count_sum[5m]))/sum(rate(scene_sphere_count_count[5m]))`,
			"scene_spheres_rendered":  `sum(rate(scene_sphere_count_sum{cache_hit="false"}[5m]))/sum(rate(scene_sphere_count_count{cache_hit="false"}[5m]))`,
			"scene_lights_requested":  `sum(rate(scene_light_count_sum[5m]))/sum(rate(scene_light_count_count[5m]))`,
			"scene_lights_rendered":   `sum(rate(scene_light_count_sum{cache_hit="false"}[5m]))/sum(rate(scene_light_count_count{cache_hit="false"}[5m]))`,
		},
	},
}

// The standard serving block: every emitter shares the aura/server_pal
// http_server_* instruments labeled by service_name, so one parameterized
// query set covers all of them. service is always a registry key, never
// caller input.
//
// Not driven by ?view=. StandardMetrics is a fixed struct whose JSON keys the
// UI reads by name, and it already carries both forms — RequestsTotal beside
// RatePerSec — so there is nothing here for a toggle to choose between.
// RequestsTotal did change: it was sum(x), cumulative since process start, and
// is now the same windowed count the custom tiles use. The field name is left
// alone so the UI keeps rendering it while it lives in another repo.
func standardScalarQueries(service string) []struct {
	Query string
	Field func(*StandardMetrics) *float64
} {
	s := fmt.Sprintf("%q", service)
	return []struct {
		Query string
		Field func(*StandardMetrics) *float64
	}{
		{`sum(increase(http_server_requests_total{service_name=` + s + `}[` + defaultCounterWindow + `]))`,
			func(m *StandardMetrics) *float64 { return &m.RequestsTotal }},
		{`sum(rate(http_server_requests_total{service_name=` + s + `}[5m]))`,
			func(m *StandardMetrics) *float64 { return &m.RatePerSec }},
		{`sum(increase(http_server_requests_success_total{service_name=` + s + `}[5m]))`,
			func(m *StandardMetrics) *float64 { return &m.SuccessCount5m }},
		{`sum(increase(http_server_requests_failure_total{service_name=` + s + `}[5m]))`,
			func(m *StandardMetrics) *float64 { return &m.FailureCount5m }},
		{`sum(rate(http_server_requests_failure_total{service_name=` + s + `}[5m]))/(sum(rate(http_server_requests_success_total{service_name=` + s + `}[5m]))+sum(rate(http_server_requests_failure_total{service_name=` + s + `}[5m])))*100`,
			func(m *StandardMetrics) *float64 { return &m.ErrorRatePercent }},
		{`sum(rate(http_server_request_duration_microseconds_sum{service_name=` + s + `}[5m]))/sum(rate(http_server_request_duration_microseconds_count{service_name=` + s + `}[5m]))`,
			func(m *StandardMetrics) *float64 { return &m.AvgDurationMicros }},
		{`histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name=` + s + `}[5m])))`,
			func(m *StandardMetrics) *float64 { return &m.P95DurationMicros }},
		{`sum(http_server_requests_active_gauge{service_name=` + s + `})`,
			func(m *StandardMetrics) *float64 { return &m.ActiveRequests }},
	}
}

// standardRequestRateQuery is the one standard timeseries built from a
// counter, so it is also the one with a count form (#1287 extended to the
// Serving charts).
//
// The count form buckets per step rather than over a fixed window like the
// scalar tiles' 5m: a chart wants one count per point, and increase() over a
// window wider than the gap between points would make adjacent buckets
// overlap and double-count requests that land near a boundary. container_
// handlers' "restarts" series already windows a range query by its own step
// for the same reason.
func standardRequestRateQuery(service, step string, view MetricView) string {
	s := fmt.Sprintf("%q", service)
	if view == ViewCount {
		return `sum(increase(http_server_requests_total{service_name=` + s + `}[` + step + `]))`
	}
	return `sum(rate(http_server_requests_total{service_name=` + s + `}[5m]))`
}

// Keys are mirrored by STANDARD_SERIES in the UI's ServiceDashboard
// (muchq.github.io); keep them in sync, and keep CustomTimeseries keys
// from colliding with them — a colliding custom series is classified as
// standard by the UI and silently never charts.
//
// Only request_rate answers to view: it is the only one of the five built
// from a counter. The other four are already ratios, quantiles, or a gauge —
// none of them has a count form to switch to, the same reasoning that keeps
// the scalar block's windowed-mean tiles fixed-form in QueryFor above.
func standardTimeseriesQueries(service, step string, view MetricView) map[string]string {
	s := fmt.Sprintf("%q", service)
	return map[string]string{
		"request_rate":       standardRequestRateQuery(service, step, view),
		"error_rate_percent": `sum(rate(http_server_requests_failure_total{service_name=` + s + `}[5m]))/(sum(rate(http_server_requests_success_total{service_name=` + s + `}[5m]))+sum(rate(http_server_requests_failure_total{service_name=` + s + `}[5m])))*100`,
		"avg_duration_us":    `sum(rate(http_server_request_duration_microseconds_sum{service_name=` + s + `}[5m]))/sum(rate(http_server_request_duration_microseconds_count{service_name=` + s + `}[5m]))`,
		"p95_duration_us":    `histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name=` + s + `}[5m])))`,
		"active_requests":    `sum(http_server_requests_active_gauge{service_name=` + s + `})`,
	}
}
