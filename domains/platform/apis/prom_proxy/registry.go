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

// Serving numbers exclude probe traffic (#1303): the container healthcheck's
// steady GET /health otherwise floors every request count (~10 per 5m at
// compose's 30s interval) and drags every latency figure toward its
// sub-millisecond durations. All three rails label their counters and
// histogram with the route (#1304, #1305), so the subtraction works
// fleet-wide; the in-flight gauge deliberately carries no route on any rail,
// and a negative matcher also matches series without the label, so the same
// filter is safe on the gauge selector — it passes through whole.
// //domains/platform/libs/otel_contract pins both label sets.
//
// Fleet-wide by decision, not accident: /health is the probe path by
// convention on every service (Caddy exposes it publicly too), so traffic
// to it is excluded from Serving everywhere, and a service that grows an
// HTTP probe adds its own Probes tile the way one_d4's entry does. The
// compose side of the /health literal is pinned by deploy's config test;
// the query side by TestRegistry_StandardServingQueriesExcludeTheProbeRoute.
//
// Composition contract: a leading comma, so it only splices after an
// existing matcher — `{service_name=` + s + probeFilter + `}`. Appended to
// an empty matcher list it is a PromQL syntax error, which is the loud
// failure you want.
const probeFilter = `,route!="/health"`

// probesTile is the standard Probes tile for a service with a compose
// healthcheck (#1307): the /health traffic excluded from every Serving
// number by probeFilter, shown as its own count so the subtraction is a
// visible fact rather than a floor under every chart. ~10/5m while the 30s
// probe is healthy; zero means the probe is failing, or the service's route
// label hasn't deployed yet. The route literal counts everything on the
// health path — Caddy exposes /health publicly, so scanner traffic to it
// also lands here rather than in Serving, and can in principle hold this
// tile above zero while the container's own probe is dead. (Wrong-method
// health requests stay on the literal on the C++/Rust rails; yodel parks
// them under its sentinel — its filter test pins that edge.)
func probesTile(service string) customScalarDef {
	return counter("Probes", "health_checks", "",
		fmt.Sprintf(`http_server_requests_total{service_name=%q,route="/health"}`, service))
}

// The window for counters that are alarms rather than volumes.
//
// Some of these tiles answer "has this happened at all" — the comment on
// runs_interrupted below says as much: anything above zero means a worker was
// stuck long enough to be given up on. Over five minutes an interruption from
// an hour ago reads zero and the alarm silently disarms, which is the failure
// mode the tile exists to prevent. A day is long enough that a human looking
// at the dashboard after the fact still sees it.
const alarmWindow = "24h"

// The window for counters over work that arrives in bursts rather than as a
// steady stream: indexing runs, and the months and archive fetches inside
// them. All of it is triggered by a person asking for a player, so between
// asks the true rate is zero and a 5m window reads zero — which is the honest
// answer to "how busy is it right now?" and the wrong answer to the question
// anyone actually opens this panel with, "did the thing I ran work?".
//
// #1313: a thousand-game run at 05:09 left a panel of zeros by breakfast,
// with nothing to distinguish it from a service that had never indexed
// anything. Same length as alarmWindow, for the same reason — the interesting
// events are sparse — but named apart because these are volumes, not alarms,
// and a future change to how long a failure stays on screen should not
// silently retune how far back the counts reach.
const burstWindow = "24h"

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

// customTimeseriesDef is one chart in a service's Trends section.
//
// A fixed one — a gauge, a ratio, a windowed mean — carries its whole
// expression in Query, the timeseries analogue of customScalarDef.Query. A
// counter-derived one sets Counter instead: the bare selector, wrapped into
// two panels rather than one whose meaning depends on ?view= — see the
// request_rate/request_count comment on standardTimeseriesQueries for why.
// The map key is the panel's base name; panels() appends _rate/_count to it,
// so a toggleable entry never has to spell either suffix twice.
type customTimeseriesDef struct {
	Query   string
	Counter string
}

func (d customTimeseriesDef) toggleable() bool { return d.Counter != "" }

// panels is the key(s) this descriptor contributes to a timeseries response:
// the map key unchanged for a fixed chart, key_rate/key_count for a
// counter-derived one. step is the chart's own bucket width, the same
// per-point windowing request_count uses and for the same reason: increase()
// over a fixed window wider than the gap between points would make adjacent
// buckets overlap and double-count.
func (d customTimeseriesDef) panels(key, step string) map[string]string {
	if !d.toggleable() {
		return map[string]string{key: d.Query}
	}
	return map[string]string{
		key + "_rate":  fmt.Sprintf("sum(rate(%s[%s]))", d.Counter, defaultCounterWindow),
		key + "_count": fmt.Sprintf("sum(increase(%s[%s]))", d.Counter, step),
	}
}

// tsCounter declares a toggleable Trends chart: its map key is the base
// name, and panels() expands it into <base>_rate and <base>_count.
func tsCounter(selector string) customTimeseriesDef {
	return customTimeseriesDef{Counter: selector}
}

// tsFixed declares a Trends chart with one form and no toggle.
func tsFixed(query string) customTimeseriesDef {
	return customTimeseriesDef{Query: query}
}

// expandCustomTimeseries flattens a service's Trends descriptors into the
// panel keys a timeseries response actually carries. A toggleable entry
// contributes two, so the capacity hint doubles the map's own length —
// only ever a hint, but len(custom) undercounts by up to 2x whenever the
// registry is mostly toggleable entries, which is the common case.
func expandCustomTimeseries(custom map[string]customTimeseriesDef, step string) map[string]string {
	out := make(map[string]string, len(custom)*2)
	for key, def := range custom {
		for panelKey, query := range def.panels(key, step) {
			out[panelKey] = query
		}
	}
	return out
}

type serviceEntry struct {
	CustomScalars []customScalarDef
	// Keyed by base name; a toggleable entry expands to two panel keys — see
	// customTimeseriesDef.panels — so this map's own keys are not always the
	// series names a timeseries response carries.
	CustomTimeseries map[string]customTimeseriesDef
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
			probesTile("golf_hub"),
			scalar("Sessions", "active", "sessions", `sum(stream_sessions_active_gauge)`),
			counter("Sessions", "started", "", `stream_sessions_total`),
			counter("Sessions", "resumed", "", `stream_sessions_total{resumed="true"}`),
			counter("Sessions", "refused", "", `stream_admissions_refused_total`),
			counter("Sessions", "disconnects", "", `stream_disconnects_total`),
			counter("Sessions", "seats_expired", "", `stream_seats_expired_total`),
			// Boot-cohort reaps (#1295): fires unattended at boot+grace, so
			// this series is the only evidence the reaper runs at all — a
			// broken one is indistinguishable from a hub with no ghosts.
			counter("Sessions", "restored_reaped", "", `restored_seats_reaped_total`),
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
		// command/event/rejection/disconnect/rate_limited/chat_message/
		// chat_delivery/chat_failure are all toggleable: each expands to a
		// _rate and a _count panel (see customTimeseriesDef.panels). Base
		// names match the pre-toggle _rate keys exactly, so a client that
		// only ever asked for the rate panel keeps reading the same series
		// name it always did.
		CustomTimeseries: map[string]customTimeseriesDef{
			"sessions_active": tsFixed(`sum(stream_sessions_active_gauge)`),
			// Fixed rather than tsCounter(stream_sessions_total): this key
			// predates the toggle and is already a count, over a plain 5m
			// window rather than the chart's own step. Making it toggleable
			// would rename it away from "session_starts" (tsCounter's base
			// key becomes both the _rate and _count panel name), breaking
			// the one client-facing name this key has ever had.
			"session_starts":     tsFixed(`sum(increase(stream_sessions_total[5m]))`),
			"command":            tsCounter(`stream_commands_total`),
			"event":              tsCounter(`stream_events_total`),
			"rejection":          tsCounter(`stream_rejections_total`),
			"disconnect":         tsCounter(`stream_disconnects_total`),
			"rate_limited":       tsCounter(`stream_rate_limited_total`),
			"chat_message":       tsCounter(`chat_appends_total{result="stored"}`),
			"chat_delivery":      tsCounter(`chat_rows_delivered_total`),
			"chat_failure":       tsCounter(`chat_failures_total`),
			"chat_catch_up_rows": tsFixed(`sum(rate(chat_catch_up_rows_sum[5m]))/sum(rate(chat_catch_up_rows_count[5m]))`),
		},
	},
	"microgpt-serve": {
		CustomScalars: []customScalarDef{
			probesTile("microgpt-serve"),
			counter("Requests by endpoint", "generate", "", `microgpt_requests_total{endpoint="generate"}`),
			counter("Requests by endpoint", "chat", "", `microgpt_requests_total{endpoint="chat"}`),
			counter("Inference", "tokens_generated", "tokens", `microgpt_tokens_generated_total`),
			scalar("Inference", "avg_duration_ms", "ms",
				`sum(rate(microgpt_request_duration_ms_sum[5m]))/sum(rate(microgpt_request_duration_ms_count[5m]))`),
			counter("Inference", "conversations", "", `microgpt_conversations_total`),
		},
		// "tokens" replaces the old "tokens_per_second" key: that name baked
		// in one form the way request_rate used to, and toggling it points
		// at tokens_rate/tokens_count instead. No other consumer names the
		// old key (grep confirms this is the only place it appeared).
		CustomTimeseries: map[string]customTimeseriesDef{
			"tokens":          tsCounter(`microgpt_tokens_generated_total`),
			"avg_duration_ms": tsFixed(`sum(rate(microgpt_request_duration_ms_sum[5m]))/sum(rate(microgpt_request_duration_ms_count[5m]))`),
		},
	},
	// The Java services (#1212): yodel's standard instruments, plus the
	// standard Probes tile now that the container is probed (#1307).
	"mcpserver": {
		CustomScalars: []customScalarDef{
			probesTile("mcpserver"),
		},
	},
	// The first Java service with a custom set, now that yodel has counters and
	// distributions to record into. Counts, outcomes and motif names only — the
	// emitter never labels by player or by game, so no series here is per-user.
	"one_d4": {
		CustomScalars: []customScalarDef{
			// The /health traffic (#1303): the container probe plus anything Caddy
			// routes there. The deploy config test and one_d4's
			// HealthProbeRouteLabelTest pin the two ways a zero here can lie.
			probesTile("one_d4"),
			counterOver("Indexing", "games_indexed", "games",
				`games_indexed_total{service_name="one_d4"}`, burstWindow),
			counterOver("Indexing", "runs_completed", "",
				`index_runs_total{service_name="one_d4",outcome="completed"}`, burstWindow),
			// The three outcomes below are alarms rather than volumes. They
			// share burstWindow's length by coincidence of both being sparse,
			// not by meaning: a failed run an hour ago is still the thing an
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
			counterOver("Indexing", "empty_months", "",
				`index_months_total{service_name="one_d4",result="empty"}`, burstWindow),
			counterOver("Indexing", "cached_months", "",
				`index_months_total{service_name="one_d4",result="cached"}`, burstWindow),
			counterOver("Indexing", "archive_fetches", "",
				`chess_com_archive_fetches_total{service_name="one_d4"}`, burstWindow),
			counterOver("Motifs", "occurrences", "",
				`motif_occurrences_total{service_name="one_d4"}`, burstWindow),
			// No motifs-per-game tile. The two counters are recorded on opposite sides of
			// the durability boundary — motifs per game inside the drain loop, games only
			// after the month's flush and period write succeed — so an interrupted or
			// lease-lost month contributes motifs and zero games. The ratio then divides
			// by a rate of zero, and any clamp large enough to avoid a divide-by-zero is
			// small enough to turn the result into a four-order-of-magnitude spike. A tile
			// that is wrong exactly when the system is unhealthy is worse than no tile.
		},
		CustomTimeseries: map[string]customTimeseriesDef{
			"games_indexed":  tsCounter(`games_indexed_total{service_name="one_d4"}`),
			"run_completion": tsCounter(`index_runs_total{service_name="one_d4",outcome="completed"}`),
			// Selected, not subtracted. In PromQL sum() over no matching series is an
			// empty vector, and vector-minus-empty is empty rather than the left side —
			// so a total-minus-completed form renders nothing on a fresh process whose
			// runs are all failing, which is precisely when someone is looking at it.
			//
			// Named outcomes rather than != "completed": lease_lost is the fourth, and it
			// is ordinary. A range changing hands mid-run happens whenever two pollers
			// overlap, so counting it here puts a permanent floor under the failure line
			// and buries the two outcomes that do mean something went wrong.
			"run_failure":         tsCounter(`index_runs_total{service_name="one_d4",outcome=~"failed|interrupted"}`),
			"run_duration_avg_us": tsFixed(`sum(rate(index_run_duration_micros_sum{service_name="one_d4",outcome="completed"}[5m]))/sum(rate(index_run_duration_micros_count{service_name="one_d4",outcome="completed"}[5m]))`),
			"motif":               tsCounter(`motif_occurrences_total{service_name="one_d4"}`),
		},
	},
	// Wordchains: server_pal's standard instruments plus the standard
	// Probes tile (#1307).
	"mithril": {
		CustomScalars: []customScalarDef{
			probesTile("mithril"),
		},
	},
	// Image blur/edges: server_pal's standard instruments plus the standard
	// Probes tile (#1307).
	"posterize": {
		CustomScalars: []customScalarDef{
			probesTile("posterize"),
		},
	},
	"portrait": {
		CustomScalars: []customScalarDef{
			probesTile("portrait"),
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
		// cache_hit_rate keeps its _rate-shaped name despite being fixed-form:
		// it is a ratio (hits over hits+misses), not a counter-derived rate,
		// so it has no _count sibling and the name predates this file's
		// convention of that suffix meaning "toggleable." Renaming it would
		// only cost clarity for a chart that was never going to pair with
		// anything.
		CustomTimeseries: map[string]customTimeseriesDef{
			"cache_hit_rate":          tsFixed(portraitCacheHitPercent),
			"cache_operations":        tsCounter(portraitCacheOps),
			"scene_spheres_requested": tsFixed(`sum(rate(scene_sphere_count_sum[5m]))/sum(rate(scene_sphere_count_count[5m]))`),
			"scene_spheres_rendered":  tsFixed(`sum(rate(scene_sphere_count_sum{cache_hit="false"}[5m]))/sum(rate(scene_sphere_count_count{cache_hit="false"}[5m]))`),
			"scene_lights_requested":  tsFixed(`sum(rate(scene_light_count_sum[5m]))/sum(rate(scene_light_count_count[5m]))`),
			"scene_lights_rendered":   tsFixed(`sum(rate(scene_light_count_sum{cache_hit="false"}[5m]))/sum(rate(scene_light_count_count{cache_hit="false"}[5m]))`),
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
		{`sum(increase(http_server_requests_total{service_name=` + s + probeFilter + `}[` + defaultCounterWindow + `]))`,
			func(m *StandardMetrics) *float64 { return &m.RequestsTotal }},
		{`sum(rate(http_server_requests_total{service_name=` + s + probeFilter + `}[5m]))`,
			func(m *StandardMetrics) *float64 { return &m.RatePerSec }},
		{`sum(increase(http_server_requests_success_total{service_name=` + s + probeFilter + `}[5m]))`,
			func(m *StandardMetrics) *float64 { return &m.SuccessCount5m }},
		{`sum(increase(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[5m]))`,
			func(m *StandardMetrics) *float64 { return &m.FailureCount5m }},
		{`sum(rate(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[5m]))/(sum(rate(http_server_requests_success_total{service_name=` + s + probeFilter + `}[5m]))+sum(rate(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[5m])))*100`,
			func(m *StandardMetrics) *float64 { return &m.ErrorRatePercent }},
		{`sum(rate(http_server_request_duration_microseconds_sum{service_name=` + s + probeFilter + `}[5m]))/sum(rate(http_server_request_duration_microseconds_count{service_name=` + s + probeFilter + `}[5m]))`,
			func(m *StandardMetrics) *float64 { return &m.AvgDurationMicros }},
		{`histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name=` + s + probeFilter + `}[5m])))`,
			func(m *StandardMetrics) *float64 { return &m.P95DurationMicros }},
		{`sum(http_server_requests_active_gauge{service_name=` + s + probeFilter + `})`,
			func(m *StandardMetrics) *float64 { return &m.ActiveRequests }},
	}
}

// Keys are mirrored by STANDARD_SERIES in the UI's ServiceDashboard
// (muchq.github.io); keep them in sync, and keep CustomTimeseries keys
// from colliding with them — a colliding custom series is classified as
// standard by the UI and silently never charts.
//
// request_count sits beside request_rate rather than replacing it under a
// ?view=-selected meaning, on the same reasoning #1287 already spelled out
// for the scalar block: a toggle that changes what an existing key means is
// a deploy hazard between two services that ship independently — a proxy new
// enough to answer the count form and a UI old enough to still expect rate()
// under that name would silently mislabel the chart, and the reverse is just
// as possible depending on rollout order. Two names, one always rate() and
// one always increase(), means either side can deploy first: an old UI never
// asks for request_count and keeps reading request_rate exactly as before: a
// new UI reading an old proxy simply finds no request_count series, the same
// "nothing there yet" a chart already renders for any absent series.
//
// The count form buckets per step rather than over a fixed window like the
// scalar tiles' 5m: a chart wants one count per point, and increase() over a
// window wider than the gap between points would make adjacent buckets
// overlap and double-count requests that land near a boundary. container_
// handlers' "restarts" series already windows a range query by its own step
// for the same reason.
//
// request_rate/request_count and error_rate_percent/error_count are the two
// pairs built from a counter: total requests and failed requests. Everything
// else here — avg/p95 latency, active requests — is a ratio, a quantile, or a
// gauge, none of which has a count form, the same reasoning that keeps the
// scalar block's windowed-mean tiles fixed-form in QueryFor above.
//
// error_count is the count of failures, not "the error rate as a count" —
// there's no such thing, since error_rate_percent is already a ratio of two
// rates rather than a windowed read of one counter. A failure count is the
// closest counter-derived analogue, the same relationship request_count has
// to request_rate.
func standardTimeseriesQueries(service, step string) map[string]string {
	s := fmt.Sprintf("%q", service)
	return map[string]string{
		"request_rate":       `sum(rate(http_server_requests_total{service_name=` + s + probeFilter + `}[5m]))`,
		"request_count":      `sum(increase(http_server_requests_total{service_name=` + s + probeFilter + `}[` + step + `]))`,
		"error_rate_percent": `sum(rate(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[5m]))/(sum(rate(http_server_requests_success_total{service_name=` + s + probeFilter + `}[5m]))+sum(rate(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[5m])))*100`,
		"error_count":        `sum(increase(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[` + step + `]))`,
		"avg_duration_us":    `sum(rate(http_server_request_duration_microseconds_sum{service_name=` + s + probeFilter + `}[5m]))/sum(rate(http_server_request_duration_microseconds_count{service_name=` + s + probeFilter + `}[5m]))`,
		"p95_duration_us":    `histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name=` + s + probeFilter + `}[5m])))`,
		"active_requests":    `sum(http_server_requests_active_gauge{service_name=` + s + probeFilter + `})`,
	}
}
