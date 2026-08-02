package prom_proxy

import "fmt"

// The service registry behind the dashboard overhaul (#1199): the catalog
// endpoint, the standard http_server_* block, and each service's custom
// metric descriptors all read from here — adding an emitter is one entry,
// no new routes, handlers, or UI code.

type customScalarDef struct {
	Group string
	Label string
	Unit  string
	Query string
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
	portraitCacheOpsRate = portraitCacheHitRate + `+` + portraitCacheMissRate
)

// Catalog order doubles as the UI's tab order.
var serviceOrder = []string{"golf_hub", "mcpserver", "microgpt-serve", "mithril", "one_d4", "portrait", "posterize"}

var serviceRegistry = map[string]serviceEntry{
	"golf_hub": {
		CustomScalars: []customScalarDef{
			{"Sessions", "active", "sessions", `sum(stream_sessions_active_gauge)`},
			{"Sessions", "started_total", "", `sum(stream_sessions_total)`},
			{"Sessions", "resumed_total", "", `sum(stream_sessions_total{resumed="true"})`},
			{"Sessions", "refused_total", "", `sum(stream_admissions_refused_total)`},
			{"Sessions", "disconnects_total", "", `sum(stream_disconnects_total)`},
			{"Sessions", "seats_expired_total", "", `sum(stream_seats_expired_total)`},
			{"Activity", "commands_per_sec", "/s", `sum(rate(stream_commands_total[5m]))`},
			{"Activity", "events_per_sec", "/s", `sum(rate(stream_events_total[5m]))`},
			{"Activity", "rejections_per_sec", "/s", `sum(rate(stream_rejections_total[5m]))`},
			{"Activity", "rate_limited_per_sec", "/s", `sum(rate(stream_rate_limited_total[5m]))`},
			// Room chat (#1226): outcome counts and stages only — the emitter
			// never labels by room, player, or text. catch_up_rows is a
			// per-drain distribution, so its windowed average is how far
			// behind a wake found an instance (the lag signal).
			{"Chat", "messages_per_sec", "/s", `sum(rate(chat_appends_total{result="stored"}[5m]))`},
			{"Chat", "stored_total", "", `sum(chat_appends_total{result="stored"})`},
			{"Chat", "delivered_rows_per_sec", "/s", `sum(rate(chat_rows_delivered_total[5m]))`},
			{"Chat", "catch_up_rows_avg_5m", "rows", `sum(rate(chat_catch_up_rows_sum[5m]))/sum(rate(chat_catch_up_rows_count[5m]))`},
			{"Chat", "history_replays_total", "", `sum(chat_history_replays_total)`},
			{"Chat", "failures_total", "", `sum(chat_failures_total)`},
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
			{"Requests by endpoint", "generate_total", "", `sum(microgpt_requests_total{endpoint="generate"})`},
			{"Requests by endpoint", "chat_total", "", `sum(microgpt_requests_total{endpoint="chat"})`},
			{"Inference", "tokens_generated_total", "tokens", `sum(microgpt_tokens_generated_total)`},
			{"Inference", "tokens_per_sec", "tokens/s", `sum(rate(microgpt_tokens_generated_total[5m]))`},
			{"Inference", "avg_duration_ms", "ms", `sum(rate(microgpt_request_duration_ms_sum[5m]))/sum(rate(microgpt_request_duration_ms_count[5m]))`},
			{"Inference", "conversations_total", "", `sum(microgpt_conversations_total)`},
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
			{"Indexing", "games_indexed_total", "games", `sum(games_indexed_total{service_name="one_d4"})`},
			{"Indexing", "games_per_sec", "/s", `sum(rate(games_indexed_total{service_name="one_d4"}[5m]))`},
			{"Indexing", "runs_completed_total", "", `sum(index_runs_total{service_name="one_d4",outcome="completed"})`},
			{"Indexing", "runs_failed_total", "", `sum(index_runs_total{service_name="one_d4",outcome="failed"})`},
			// A wedge cut loose by the MAX_RUN ceiling lands here (#1282). Anything
			// above zero means a worker was stuck long enough to be given up on.
			{"Indexing", "runs_interrupted_total", "", `sum(index_runs_total{service_name="one_d4",outcome="interrupted"})`},
			// Emitted since the ceiling landed, and listed so the four outcomes add up to
			// index_runs_total. A range changing hands mid-run is ordinary; a rising count
			// beside a flat interrupted count is contention, not a wedge.
			{"Indexing", "runs_lease_lost_total", "", `sum(index_runs_total{service_name="one_d4",outcome="lease_lost"})`},
			// Windowed averages over the histograms: rate(sum)/rate(count) is the
			// mean per run in the window, the same shape portrait uses.
			// Completed runs only. The histogram is labelled by outcome, and an interrupted
			// run is one that sat at the MAX_RUN ceiling — pooling those in makes the average
			// spike at exactly the moment someone is reading it to size a real run.
			{"Indexing", "avg_run_seconds_1h", "s", `sum(rate(index_run_duration_micros_sum{service_name="one_d4",outcome="completed"}[1h]))/sum(rate(index_run_duration_micros_count{service_name="one_d4",outcome="completed"}[1h]))/1000000`},
			{"Indexing", "avg_games_per_month_1h", "games", `sum(rate(index_games_per_month_sum{service_name="one_d4"}[1h]))/sum(rate(index_games_per_month_count{service_name="one_d4"}[1h]))`},
			{"Indexing", "empty_months_total", "", `sum(index_months_total{service_name="one_d4",result="empty"})`},
			{"Indexing", "cached_months_total", "", `sum(index_months_total{service_name="one_d4",result="cached"})`},
			{"Indexing", "archive_fetches_per_sec", "/s", `sum(rate(chess_com_archive_fetches_total{service_name="one_d4"}[5m]))`},
			{"Motifs", "occurrences_per_sec", "/s", `sum(rate(motif_occurrences_total{service_name="one_d4"}[5m]))`},
			{"Motifs", "occurrences_total", "", `sum(motif_occurrences_total{service_name="one_d4"})`},
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
			{"Render cache", "hit_rate_percent", "%", portraitCacheHitPercent},
			{"Render cache", "operations_per_sec", "/s", portraitCacheOpsRate},
			// Windowed averages over RecordDistribution histograms:
			// rate(sum)/rate(count) = mean per request in the window.
			{"Scene complexity", "avg_spheres_1h", "spheres", `sum(rate(scene_sphere_count_sum[1h]))/sum(rate(scene_sphere_count_count[1h]))`},
			{"Scene complexity", "avg_lights_1h", "lights", `sum(rate(scene_light_count_sum[1h]))/sum(rate(scene_light_count_count[1h]))`},
		},
		CustomTimeseries: map[string]string{
			"cache_hit_rate":        portraitCacheHitPercent,
			"cache_operations_rate": portraitCacheOpsRate,
			"scene_sphere_count":    `sum(rate(scene_sphere_count_sum[5m]))/sum(rate(scene_sphere_count_count[5m]))`,
			"scene_light_count":     `sum(rate(scene_light_count_sum[5m]))/sum(rate(scene_light_count_count[5m]))`,
		},
	},
}

// The standard serving block: every emitter shares the aura/server_pal
// http_server_* instruments labeled by service_name, so one parameterized
// query set covers all of them. service is always a registry key, never
// caller input.
func standardScalarQueries(service string) []struct {
	Query string
	Field func(*StandardMetrics) *float64
} {
	s := fmt.Sprintf("%q", service)
	return []struct {
		Query string
		Field func(*StandardMetrics) *float64
	}{
		{`sum(http_server_requests_total{service_name=` + s + `})`,
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

// Keys are mirrored by STANDARD_SERIES in the UI's ServiceDashboard
// (muchq.github.io); keep them in sync, and keep CustomTimeseries keys
// from colliding with them — a colliding custom series is classified as
// standard by the UI and silently never charts.
func standardTimeseriesQueries(service string) map[string]string {
	s := fmt.Sprintf("%q", service)
	return map[string]string{
		"request_rate":       `sum(rate(http_server_requests_total{service_name=` + s + `}[5m]))`,
		"error_rate_percent": `sum(rate(http_server_requests_failure_total{service_name=` + s + `}[5m]))/(sum(rate(http_server_requests_success_total{service_name=` + s + `}[5m]))+sum(rate(http_server_requests_failure_total{service_name=` + s + `}[5m])))*100`,
		"avg_duration_us":    `sum(rate(http_server_request_duration_microseconds_sum{service_name=` + s + `}[5m]))/sum(rate(http_server_request_duration_microseconds_count{service_name=` + s + `}[5m]))`,
		"p95_duration_us":    `histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name=` + s + `}[5m])))`,
		"active_requests":    `sum(http_server_requests_active_gauge{service_name=` + s + `})`,
	}
}
