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

// Catalog order doubles as the UI's tab order.
var serviceOrder = []string{"golf_hub", "microgpt-serve", "mithril", "portrait"}

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
		},
		CustomTimeseries: map[string]string{
			"sessions_active": `sum(stream_sessions_active_gauge)`,
			"session_starts":  `sum(increase(stream_sessions_total[5m]))`,
			"command_rate":    `sum(rate(stream_commands_total[5m]))`,
			"event_rate":      `sum(rate(stream_events_total[5m]))`,
			"rejection_rate":  `sum(rate(stream_rejections_total[5m]))`,
			"disconnect_rate": `sum(rate(stream_disconnects_total[5m]))`,
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
	// Wordchains: server_pal's standard instruments only, no custom set.
	"mithril": {},
	"portrait": {
		CustomScalars: []customScalarDef{
			{"Render cache", "hit_rate_percent", "%", `rate(trace_cache_hits_total[5m])/(rate(trace_cache_hits_total[5m])+rate(trace_cache_misses_total[5m]))*100`},
			{"Render cache", "operations_per_sec", "/s", `rate(trace_cache_hits_total[5m])+rate(trace_cache_misses_total[5m])`},
			{"Scene complexity", "avg_spheres_1h", "spheres", `sum(rate(scene_sphere_count_sum[1h]))/sum(rate(scene_sphere_count_count[1h]))`},
			{"Scene complexity", "avg_lights_1h", "lights", `sum(rate(scene_light_count_sum[1h]))/sum(rate(scene_light_count_count[1h]))`},
		},
		CustomTimeseries: map[string]string{
			"cache_hit_rate":        `rate(trace_cache_hits_total[5m])/(rate(trace_cache_hits_total[5m])+rate(trace_cache_misses_total[5m]))*100`,
			"cache_operations_rate": `rate(trace_cache_hits_total[5m])+rate(trace_cache_misses_total[5m])`,
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
