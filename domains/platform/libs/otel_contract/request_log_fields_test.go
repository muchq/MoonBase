package otel_contract

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

// The request-log field vocabulary (#1459): the C++ and Rust rails each
// emit one JSON object per request, and a reader who pivots from a
// dashboard to a log query must find the same words on both — including
// the label names the metric contract already pins (http_method, route,
// service_name). Like the sentinel test above, this pins only the
// cross-language spelling; that the fields actually reach the wire is each
// rail's own behavioral test (aura's middleware_test reads the served line
// off a mock log sink, server_pal's access_log tests parse the subscriber's
// JSON output).
//
// The Java rail is deliberately absent: one_d4 and mcpserver emit JSON app
// logs (logback's JsonEncoder, pinned by LogbackConfigTest) but no
// per-request access line — Caddy fronts every route they serve.
var requestLogFields = []string{
	"service_name",
	"http_method",
	"route",
	"target",
	"status",
	"duration_us",
	"trace_id",
	"x_forwarded_for",
}

func TestRequestLogFieldSpellingAgreesAcrossRails(t *testing.T) {
	emitters := []struct {
		path   string
		marker string
	}{
		{path: "../aura/middleware.cc", marker: "AppendJsonField"},
		{path: "../server_pal/src/lib.rs", marker: "access_log_middleware"},
	}
	for _, emitter := range emitters {
		source := string(codeLines(t, emitter.path, emitter.marker))
		for _, field := range requestLogFields {
			assert.Contains(t, source, field,
				"%s does not name request-log field %q; the two rails' lines no longer "+
					"speak one vocabulary and a cross-service log query silently misses "+
					"this rail", emitter.path, field)
		}
	}
}
