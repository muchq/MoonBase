package prom_proxy

import (
	"fmt"
	"time"
)

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
// #1323: a thousand-game run at 05:09 left a panel of zeros by breakfast,
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
// A counter-ratio mean sets MeanNumerator/MeanDenominator instead of Query,
// so its rate() window can track the chart's step the way the standard
// latency charts do. The map key is the panel's base name; panels() appends
// _rate/_count to it, so a toggleable entry never has to spell either suffix
// twice.
type customTimeseriesDef struct {
	Query           string
	Counter         string
	MeanNumerator   string
	MeanDenominator string
	// Appended verbatim after a mean's ratio — a unit conversion like
	// "/1000", never a selector.
	MeanScale string
}

func (d customTimeseriesDef) toggleable() bool { return d.Counter != "" }

// panels is the key(s) this descriptor contributes to a timeseries response:
// the map key unchanged for a fixed chart, key_rate/key_count for a
// counter-derived one. step is the chart's own bucket width, the same
// per-point windowing request_count uses and for the same reason: increase()
// over a fixed window wider than the gap between points would make adjacent
// buckets overlap and double-count. A mean chart windows by latencyWindow
// like avg_duration_us, and for the same reason: a fixed 5m rate() inside a
// 7d chart's 1h step reads five minutes of every hour and draws zero for the
// other fifty-five.
func (d customTimeseriesDef) panels(key, step string) map[string]string {
	if d.MeanNumerator != "" {
		w := latencyWindow(step)
		return map[string]string{key: fmt.Sprintf("sum(rate(%s[%s]))/sum(rate(%s[%s]))%s",
			d.MeanNumerator, w, d.MeanDenominator, w, d.MeanScale)}
	}
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

// tsMean declares a fixed-form windowed-mean chart over two counters the
// emitter declares at zero (#1384): rate(numerator)/rate(denominator), both
// over latencyWindow(step).
func tsMean(numerator, denominator string) customTimeseriesDef {
	return customTimeseriesDef{MeanNumerator: numerator, MeanDenominator: denominator}
}

// tsMeanScaled is tsMean with a unit conversion appended to the ratio, for a
// chart whose key names a different unit than the counters carry.
func tsMeanScaled(numerator, denominator, scale string) customTimeseriesDef {
	return customTimeseriesDef{
		MeanNumerator: numerator, MeanDenominator: denominator, MeanScale: scale}
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

// aura::Cache emitters share the standard cache family (#1209), selected on
// both labels: which service, and which cache within it. Parameterized —
// the prom_proxy half of #1209 — now that iili's url_cache is the second
// emitter (#1359).
func cacheHitPercent(service, cache string) string {
	hit := fmt.Sprintf(`rate(cache_hits_total{service_name=%q,cache=%q}[5m])`, service, cache)
	miss := fmt.Sprintf(`rate(cache_misses_total{service_name=%q,cache=%q}[5m])`, service, cache)
	return hit + `/(` + hit + `+` + miss + `)*100`
}

// Both cache counters as one selector, so the operations tile can be built
// in either view from a single expression. Selected by __name__ rather
// than summed as rate(hits)+rate(misses): that form is two instant vectors
// joined by binary matching, which yields nothing at all if one side has
// no series yet — a fresh process whose cache has only ever missed.
func cacheOps(service, cache string) string {
	return fmt.Sprintf(
		`{__name__=~"cache_hits_total|cache_misses_total",service_name=%q,cache=%q}`,
		service, cache)
}

// Scene complexity is recorded on both cache paths and labelled by cache_hit
// (#1287). Summing across the label is offered load — what callers asked to
// render; selecting cache_hit="false" is render cost — what the tracer drew.
// The panel used to show only the second under a name that read like the
// first.
//
// Counter ratios, not histogram halves (#1452): scene_spheres/scene_lights
// accumulate per-scene totals and trace_scenes counts the scenes, all
// declared at zero by the tracer, so the first render after a deploy moves
// every one of these.
const (
	portraitSpheresRequested = `sum(rate(scene_spheres_total[1h]))/` +
		`sum(rate(trace_scenes_total[1h]))`
	portraitSpheresRendered = `sum(rate(scene_spheres_total{cache_hit="false"}[1h]))/` +
		`sum(rate(trace_scenes_total{cache_hit="false"}[1h]))`
	portraitLightsRequested = `sum(rate(scene_lights_total[1h]))/` +
		`sum(rate(trace_scenes_total[1h]))`
	portraitLightsRendered = `sum(rate(scene_lights_total{cache_hit="false"}[1h]))/` +
		`sum(rate(trace_scenes_total{cache_hit="false"}[1h]))`
)

// Catalog order doubles as the UI's tab order.
var serviceOrder = []string{"games_hub", "mcpserver", "microgpt-serve", "mithril", "one_d4", "one_d4_v2", "portrait", "posterize", "iili"}

var serviceRegistry = map[string]serviceEntry{
	"games_hub": {
		CustomScalars: []customScalarDef{
			probesTile("games_hub"),
			scalar("Golf sessions", "golf_active", "sessions", `sum(golf_sessions_active_gauge)`),
			counter("Golf sessions", "golf_started", "", `golf_sessions_total`),
			counter("Golf sessions", "golf_resumed", "", `golf_sessions_total{resumed="true"}`),
			counter("Golf sessions", "golf_refused", "", `golf_admissions_refused_total`),
			counter("Golf sessions", "golf_disconnects", "", `golf_disconnects_total`),
			counter("Golf sessions", "golf_seats_expired", "", `golf_seats_expired_total`),
			// Boot-cohort reaps (#1295): fires unattended at boot+grace, so
			// this series is the only evidence the reaper runs at all — a
			// broken one is indistinguishable from a hub with no ghosts.
			counter("Golf sessions", "golf_restored_reaped", "", `golf_restored_seats_reaped_total`),
			counter("Golf activity", "golf_commands", "", `golf_commands_total`),
			counter("Golf activity", "golf_events", "", `golf_events_total`),
			counter("Golf activity", "golf_rejections", "", `golf_rejections_total`),
			counter("Golf activity", "golf_rate_limited", "", `golf_rate_limited_total`),
			// Room chat (#1226): outcome counts and stages only — the emitter
			// never labels by room, player, or text. catch_up_rows_avg is rows
			// per drain — how far behind a wake found an instance (the lag
			// signal) — computed from two counters the hub declares at zero
			// (#1384): the delivered rows over every drain, empty drains
			// included. The zero-row drains are in the denominator on purpose;
			// dropping them would inflate the average exactly when the hub is
			// keeping up. A mean rather than a count, so it has no rate form.
			counter("Chat", "messages", "", `chat_appends_total{result="stored"}`),
			counter("Chat", "delivered_rows", "rows", `chat_rows_delivered_total`),
			scalar("Chat", "catch_up_rows_avg_5m", "rows",
				`sum(rate(chat_rows_delivered_total[5m]))/sum(rate(chat_catch_up_drains_total[5m]))`),
			counter("Chat", "history_replays", "", `chat_history_replays_total`),
			counterOver("Chat", "failures", "", `chat_failures_total`, alarmWindow),
			// Thoughts (#79): the second hub in the process, on its own
			// thoughts_* series. Series and labels carry the game prefix
			// (golf_, thoughts_) because a label is a tile's identity across
			// the whole service, not just its group; chat_* stays bare
			// because chat belongs to the room layer, not to golf.
			scalar("Thoughts", "thoughts_active", "sessions", `sum(thoughts_sessions_active_gauge)`),
			counter("Thoughts", "thoughts_sessions", "", `thoughts_sessions_total`),
			counter("Thoughts", "thoughts_refused", "", `thoughts_admissions_refused_total`),
			counter("Thoughts", "thoughts_commands", "", `thoughts_commands_total`),
			counter("Thoughts", "thoughts_events", "", `thoughts_events_total`),
			counter("Thoughts", "thoughts_rejections", "", `thoughts_rejections_total`),
			counter("Thoughts", "thoughts_rate_limited", "", `thoughts_rate_limited_total`),
			counter("Thoughts", "thoughts_disconnects", "", `thoughts_disconnects_total`),
			// Castle (#77) rides the room stream, so its sessions and
			// rejections are the golf rows above; only its envelope has
			// series of its own.
			counter("Castle", "castle_commands", "", `castle_commands_total`),
			counter("Castle", "castle_events", "", `castle_events_total`),
		},
		// command/event/rejection/disconnect/rate_limited/chat_message/
		// chat_delivery/chat_failure are all toggleable: each expands to a
		// _rate and a _count panel (see customTimeseriesDef.panels). Base
		// names match the pre-toggle _rate keys exactly, so a client that
		// only ever asked for the rate panel keeps reading the same series
		// name it always did.
		CustomTimeseries: map[string]customTimeseriesDef{
			"sessions_active": tsFixed(`sum(golf_sessions_active_gauge)`),
			// Fixed rather than tsCounter(golf_sessions_total): this key
			// predates the toggle and is already a count, over a plain 5m
			// window rather than the chart's own step. Making it toggleable
			// would rename it away from "session_starts" (tsCounter's base
			// key becomes both the _rate and _count panel name), breaking
			// the one client-facing name this key has ever had.
			"session_starts":     tsFixed(`sum(increase(golf_sessions_total[5m]))`),
			"command":            tsCounter(`golf_commands_total`),
			"event":              tsCounter(`golf_events_total`),
			"rejection":          tsCounter(`golf_rejections_total`),
			"disconnect":         tsCounter(`golf_disconnects_total`),
			"rate_limited":       tsCounter(`golf_rate_limited_total`),
			"chat_message":       tsCounter(`chat_appends_total{result="stored"}`),
			"chat_delivery":      tsCounter(`chat_rows_delivered_total`),
			"chat_failure":       tsCounter(`chat_failures_total`),
			"chat_catch_up_rows": tsMean(`chat_rows_delivered_total`, `chat_catch_up_drains_total`),
			"thoughts_active":    tsFixed(`sum(thoughts_sessions_active_gauge)`),
			"thoughts_command":   tsCounter(`thoughts_commands_total`),
			"thoughts_event":     tsCounter(`thoughts_events_total`),
			"thoughts_rejection": tsCounter(`thoughts_rejections_total`),
			"castle_command":     tsCounter(`castle_commands_total`),
			"castle_event":       tsCounter(`castle_events_total`),
		},
	},
	"microgpt-serve": {
		// The inference means read counters, not histogram halves (#1384):
		// microgpt_inference_ms_total is the rail's cumulative duration,
		// declared at zero alongside the request and token counts, so the
		// first request after a deploy moves every one of these. Mean request
		// duration divides it by the request count; tokens per second of
		// inference divides the token count by it — the ratio-of-rates form,
		// total tokens over total model time in the window.
		CustomScalars: []customScalarDef{
			probesTile("microgpt-serve"),
			counter("Requests by endpoint", "generate", "", `microgpt_requests_total{endpoint="generate"}`),
			counter("Requests by endpoint", "chat", "", `microgpt_requests_total{endpoint="chat"}`),
			counter("Inference", "tokens_generated", "tokens", `microgpt_tokens_generated_total`),
			scalar("Inference", "avg_duration_ms", "ms",
				`sum(rate(microgpt_inference_ms_total[5m]))/sum(rate(microgpt_requests_total[5m]))`),
			scalar("Inference", "tokens_per_sec_5m", "tok/s",
				`sum(rate(microgpt_tokens_generated_total[5m]))/sum(rate(microgpt_inference_ms_total[5m]))*1000`),
			counter("Inference", "conversations", "", `microgpt_conversations_total`),
		},
		// "tokens" replaces the old "tokens_per_second" key: that name baked
		// in one form the way request_rate used to, and toggling it points
		// at tokens_rate/tokens_count instead. No other consumer names the
		// old key (grep confirms this is the only place it appeared).
		CustomTimeseries: map[string]customTimeseriesDef{
			"tokens":          tsCounter(`microgpt_tokens_generated_total`),
			"avg_duration_ms": tsMean(`microgpt_inference_ms_total`, `microgpt_requests_total`),
		},
	},
	// The Java services (#1212): yodel's standard instruments, plus the
	// standard Probes tile now that the container is probed (#1307).
	"mcpserver": {
		CustomScalars: []customScalarDef{
			probesTile("mcpserver"),
		},
	},
	// Counts, outcomes and motif names only — the emitter never labels by
	// player or by game, so no series here is per-user.
	//
	// The indexing series come from one_d4_worker, the name worker_main.cc
	// reports as. The service_name=~"one_d4(_worker)?" selectors also match
	// stored series recorded under service_name="one_d4"; both names are one
	// timeline, and narrowing the selector would cut every chart off where
	// they meet. Nothing enforces the match with the worker: renaming the
	// service there leaves every test in this repo green and every indexing
	// chart Java-only.
	// The probes tile stays scoped to one_d4: the worker serves no HTTP.
	"one_d4": {
		CustomScalars: []customScalarDef{
			// The /health traffic (#1303): the container probe plus anything Caddy
			// routes there. The deploy config test and one_d4's
			// HealthProbeRouteLabelTest pin the two ways a zero here can lie.
			probesTile("one_d4"),
			// Query events (#1465): the bounded half rides here, the shape in the
			// logs. Emitted by the Java service, so the shared selector's worker
			// half matches nothing today; kept for the day it does.
			counter("Queries", "served", "",
				`one_d4_queries_total{service_name=~"one_d4(_worker)?",outcome="ok"}`),
			counterOver("Queries", "failed", "",
				`one_d4_queries_total{service_name=~"one_d4(_worker)?",outcome="failed"}`, alarmWindow),
			counterOver("Indexing", "games_indexed", "games",
				`games_indexed_total{service_name=~"one_d4(_worker)?"}`, burstWindow),
			counterOver("Indexing", "runs_completed", "",
				`index_runs_total{service_name=~"one_d4(_worker)?",outcome="completed"}`, burstWindow),
			// The three outcomes below are alarms rather than volumes. They
			// share burstWindow's length by coincidence of both being sparse,
			// not by meaning: a failed run an hour ago is still the thing an
			// operator opened this page to find.
			counterOver("Indexing", "runs_failed", "",
				`index_runs_total{service_name=~"one_d4(_worker)?",outcome="failed"}`, alarmWindow),
			// A wedge cut loose by the MAX_RUN ceiling lands here (#1282). Anything
			// above zero means a worker was stuck long enough to be given up on.
			counterOver("Indexing", "runs_interrupted", "",
				`index_runs_total{service_name=~"one_d4(_worker)?",outcome="interrupted"}`, alarmWindow),
			// Emitted since the ceiling landed, and listed so the four outcomes add up to
			// index_runs_total. A range changing hands mid-run is ordinary; a rising count
			// beside a flat interrupted count is contention, not a wedge — which only
			// reads that way if the two share a window, hence the same one.
			counterOver("Indexing", "runs_lease_lost", "",
				`index_runs_total{service_name=~"one_d4(_worker)?",outcome="lease_lost"}`, alarmWindow),
			// Windowed means as counter ratios (#1452): the worker records
			// cumulative run microseconds 1:1 beside the run counter, and its
			// games land beside the months that measured them, so each mean
			// divides two series declared at zero. Means, so no rate form.
			// Completed runs only: an interrupted run is one that sat at the
			// MAX_RUN ceiling — pooling those in makes the average spike at
			// exactly the moment someone is reading it to size a real run.
			scalar("Indexing", "avg_run_seconds_1h", "s",
				`sum(rate(index_run_duration_micros_total{service_name=~"one_d4(_worker)?",outcome="completed"}[1h]))/sum(rate(index_runs_total{service_name=~"one_d4(_worker)?",outcome="completed"}[1h]))/1000000`),
			// The measured results only — an empty or cached month feeds the
			// numerator nothing, so counting it in the denominator would make
			// a decade-long backfill read as tiny archives.
			scalar("Indexing", "avg_games_per_month_1h", "games",
				`sum(rate(games_indexed_total{service_name=~"one_d4(_worker)?"}[1h]))/sum(rate(index_months_total{service_name=~"one_d4(_worker)?",result=~"indexed|degraded"}[1h]))`),
			counterOver("Indexing", "empty_months", "",
				`index_months_total{service_name=~"one_d4(_worker)?",result="empty"}`, burstWindow),
			counterOver("Indexing", "cached_months", "",
				`index_months_total{service_name=~"one_d4(_worker)?",result="cached"}`, burstWindow),
			counterOver("Indexing", "archive_fetches", "",
				`chess_com_archive_fetches_total{service_name=~"one_d4(_worker)?"}`, burstWindow),
			counterOver("Motifs", "occurrences", "",
				`motif_occurrences_total{service_name=~"one_d4(_worker)?"}`, burstWindow),
			// No motifs-per-game tile. The two counters are recorded on opposite sides of
			// the durability boundary — motifs per game inside the drain loop, games only
			// after the month's flush and period write succeed — so an interrupted or
			// lease-lost month contributes motifs and zero games. The ratio then divides
			// by a rate of zero, and any clamp large enough to avoid a divide-by-zero is
			// small enough to turn the result into a four-order-of-magnitude spike. A tile
			// that is wrong exactly when the system is unhealthy is worse than no tile.

			// Cleanup (#1356, #1424). The sweep used to delete in silence, which
			// made "it stopped running" and "there was nothing to delete"
			// indistinguishable from here. sweeps is the tile that separates
			// them: it counts every pass, so a sweep that dies shows up as this
			// falling to zero while the others merely stay there.
			counterOver("Cleanup", "sweeps", "",
				`retention_sweeps_total{service_name=~"one_d4(_worker)?",outcome="ok"}`, burstWindow),
			// An alarm, not a volume: a sweep that cannot reach the database
			// leaves rows uncollected and requests unsettled, and neither is
			// visible in any other tile.
			counterOver("Cleanup", "sweeps_failed", "",
				`retention_sweeps_total{service_name=~"one_d4(_worker)?",outcome="error"}`, alarmWindow),
			counterOver("Cleanup", "rows_deleted", "rows",
				`retention_rows_deleted_total{service_name=~"one_d4(_worker)?"}`, burstWindow),
			// Requeued work, which is ordinary — a worker died and another took
			// its range.
			counterOver("Cleanup", "requests_requeued", "",
				`retention_requests_settled_total{service_name=~"one_d4(_worker)?",arm="released"}`, burstWindow),
			// The two that end a request rather than moving it. Both mean a user
			// got an answer they did not want, so both read on the alarm window:
			// poisoned is a range that fails repeatedly, stalled is a fleet that
			// was not running at all.
			counterOver("Cleanup", "requests_poisoned", "",
				`retention_requests_settled_total{service_name=~"one_d4(_worker)?",arm="poisoned"}`, alarmWindow),
			counterOver("Cleanup", "requests_stalled", "",
				`retention_requests_settled_total{service_name=~"one_d4(_worker)?",arm="stalled"}`, alarmWindow),
		},
		CustomTimeseries: map[string]customTimeseriesDef{
			"games_indexed":  tsCounter(`games_indexed_total{service_name=~"one_d4(_worker)?"}`),
			"run_completion": tsCounter(`index_runs_total{service_name=~"one_d4(_worker)?",outcome="completed"}`),
			// Selected, not subtracted. In PromQL sum() over no matching series is an
			// empty vector, and vector-minus-empty is empty rather than the left side —
			// so a total-minus-completed form renders nothing on a fresh process whose
			// runs are all failing, which is precisely when someone is looking at it.
			//
			// Named outcomes rather than != "completed": lease_lost is the fourth, and it
			// is ordinary. A range changing hands mid-run happens whenever two pollers
			// overlap, so counting it here puts a permanent floor under the failure line
			// and buries the two outcomes that do mean something went wrong.
			"run_failure": tsCounter(`index_runs_total{service_name=~"one_d4(_worker)?",outcome=~"failed|interrupted"}`),
			// Converted to milliseconds. A Trends series carries no unit in the payload —
			// the chart title is the key title-cased, and nothing else on it says what the
			// numbers are — so the key names the unit and the query has to match it. The
			// counter accumulates microseconds because the stored series do: unconverted,
			// a two-minute run plots as 120000000.
			"run_duration_avg_ms": tsMeanScaled(
				`index_run_duration_micros_total{service_name=~"one_d4(_worker)?",outcome="completed"}`,
				`index_runs_total{service_name=~"one_d4(_worker)?",outcome="completed"}`,
				"/1000"),
			"motif": tsCounter(`motif_occurrences_total{service_name=~"one_d4(_worker)?"}`),
		},
	},
	// The C++ analyze service (#1389 phase 6): aura's standard instruments
	// plus the standard Probes tile. Deliberately not folded into one_d4's
	// service_name=~"one_d4(_worker)?" selectors: those cover the two
	// processes indexing into one table, and analyze writes nothing — its
	// serving numbers answer a different question and belong on their own
	// tab. Rate-limit rejections land in the standard failure counters
	// (RejectionMetrics), so no custom tile is needed for them.
	"one_d4_v2": {
		CustomScalars: []customScalarDef{
			probesTile("one_d4_v2"),
		},
	},
	// iili (#1359): standard instruments, Probes, and the URL cache.
	"iili": {
		CustomScalars: []customScalarDef{
			probesTile("iili"),
			scalar("URL cache", "hit_rate_percent", "%", cacheHitPercent("iili", "url_cache")),
			counter("URL cache", "operations", "", cacheOps("iili", "url_cache")),
		},
		CustomTimeseries: map[string]customTimeseriesDef{
			"cache_hit_rate":   tsFixed(cacheHitPercent("iili", "url_cache")),
			"cache_operations": tsCounter(cacheOps("iili", "url_cache")),
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
			scalar("Render cache", "hit_rate_percent", "%", cacheHitPercent("portrait", "trace")),
			counter("Render cache", "operations", "", cacheOps("portrait", "trace")),
			// Windowed means as counter ratios (#1452): per-scene sums over
			// the trace_scenes denominator, all declared at zero by the
			// tracer. Requested is every accepted request; rendered is the
			// cache misses the tracer actually drew.
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
			"cache_hit_rate":          tsFixed(cacheHitPercent("portrait", "trace")),
			"cache_operations":        tsCounter(cacheOps("portrait", "trace")),
			"scene_spheres_requested": tsMean(`scene_spheres_total`, `trace_scenes_total`),
			"scene_spheres_rendered": tsMean(`scene_spheres_total{cache_hit="false"}`,
				`trace_scenes_total{cache_hit="false"}`),
			"scene_lights_requested": tsMean(`scene_lights_total`, `trace_scenes_total`),
			"scene_lights_rendered": tsMean(`scene_lights_total{cache_hit="false"}`,
				`trace_scenes_total{cache_hit="false"}`),
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
//
// avg_duration_us and p95_duration_us track the step too, unlike
// request_rate: a fixed 5m rate() window is fine when the chart's points are
// closer together than 5m (30m's 30s step is), but at 7d the step is 1h — a
// rate() evaluated once per hour then only ever sees the last five minutes
// of it, the same blind spot restarts' window comment in
// container_handlers.go describes for changes(). A service without
// near-continuous traffic — posterize, sampled a handful of times a day —
// showed a flat 0 in every bucket despite the histogram genuinely recording
// every one of those requests. latencyWindow keeps 5m as a floor and widens
// past it once step plus one scrape interval exceeds it — which, at 1d's 5m
// step, is true too: see latencyWindow's own comment for why the padding
// matters even when step alone wouldn't have widened anything.
func standardTimeseriesQueries(service, step string) map[string]string {
	s := fmt.Sprintf("%q", service)
	w := latencyWindow(step)
	return map[string]string{
		"request_rate":       `sum(rate(http_server_requests_total{service_name=` + s + probeFilter + `}[5m]))`,
		"request_count":      `sum(increase(http_server_requests_total{service_name=` + s + probeFilter + `}[` + step + `]))`,
		"error_rate_percent": `sum(rate(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[5m]))/(sum(rate(http_server_requests_success_total{service_name=` + s + probeFilter + `}[5m]))+sum(rate(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[5m])))*100`,
		"error_count":        `sum(increase(http_server_requests_failure_total{service_name=` + s + probeFilter + `}[` + step + `]))`,
		"avg_duration_us":    `sum(rate(http_server_request_duration_microseconds_sum{service_name=` + s + probeFilter + `}[` + w + `]))/sum(rate(http_server_request_duration_microseconds_count{service_name=` + s + probeFilter + `}[` + w + `]))`,
		"p95_duration_us":    `histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name=` + s + probeFilter + `}[` + w + `])))`,
		"active_requests":    `sum(http_server_requests_active_gauge{service_name=` + s + probeFilter + `})`,
	}
}

// The Prometheus scrape interval configured in
// deploy/consolidated/o11y/prometheus.yml. Every server_pal/aura/yodel
// emitter is scraped at the same interval, so one constant covers the fleet.
const scrapeInterval = 15 * time.Second

// latencyWindow is the rate() lookback avg_duration_us and p95_duration_us
// use in a timeseries chart: at least defaultCounterWindow (5m), widened to
// the chart's own step plus one scrape interval whenever that sum is larger.
//
// The +scrapeInterval matters even once step alone already clears 5m.
// Prometheus range vectors are left-open — (T-window, T] — so a request
// landing just after a window's left boundary but before that window's
// first scrape sample is invisible to rate() there, and the previous window
// ended before the request happened at all: a window equal to exactly step
// leaves that boundary gap open regardless of how wide step is. Padding by
// one scrape interval past step closes it. See
// https://prometheus.io/docs/prometheus/latest/querying/basics/#range-vector-selectors.
//
// An unparsable step (never produced by GetTimeRangeConfig, but this has no
// other caller to lean on that) falls back to defaultCounterWindow rather
// than propagating a broken duration string into three PromQL queries.
func latencyWindow(step string) string {
	stepDuration, err := time.ParseDuration(step)
	if err != nil {
		return defaultCounterWindow
	}
	widened := stepDuration + scrapeInterval
	if widened <= 5*time.Minute {
		return defaultCounterWindow
	}
	return widened.String()
}
