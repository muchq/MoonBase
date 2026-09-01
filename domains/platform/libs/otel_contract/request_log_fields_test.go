package otel_contract

import (
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The request-log field vocabulary (#1459): the C++ and Rust rails each
// emit one JSON object per request, and a reader who pivots from a
// dashboard to a log query must find the same words on both — including
// the label names the metric contract already pins (http_method, route,
// service_name). Like the sentinel test in http_instrument_labels_test.go,
// this pins only the
// cross-language spelling; that the fields actually reach the wire is each
// rail's own behavioral test (aura's middleware_test reads the served line
// off a mock log sink, server_pal's access_log tests parse the subscriber's
// JSON output).
//
// The Java rail is deliberately absent: one_d4 and mcpserver emit JSON app
// logs (logback's JsonEncoder, pinned by LogbackConfigTest) but no
// per-request access line — Caddy's log covers their external traffic, and
// compose-internal calls (mcpserver -> one_d4) are the accepted gap.
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

// response_bytes is deliberately not in the list: the C++ rail emits it and
// the Rust rail cannot without wrapping the response body to count it, so a
// cross-service response_bytes query covers the C++ services only.

func TestRequestLogFieldSpellingAgreesAcrossRails(t *testing.T) {
	// C++: every emitted key is a quoted string literal handed to
	// AppendJsonField / AppendJsonNumber, so the pin matches `"key"` — a
	// bare identifier elsewhere in the file cannot satisfy it.
	aura := string(codeLines(t, "../aura/middleware.cc", "AppendJsonField"))
	for _, field := range requestLogFields {
		assert.Contains(t, aura, `"`+field+`"`,
			"aura's access line no longer emits a %q key; the two rails' lines no "+
				"longer speak one vocabulary and a cross-service log query silently "+
				"misses this rail", field)
	}

	// Rust: the emitted keys are the field names inside the one
	// tracing::info! block, so the pin reads only that block — a local
	// variable elsewhere cannot satisfy it.
	rust := string(codeLines(t, "../server_pal/src/lib.rs", "access_log_middleware"))
	start := strings.Index(rust, "tracing::info!(")
	require.GreaterOrEqual(t, start, 0, "no tracing::info! block in access_log_middleware")
	end := strings.Index(rust[start:], ");")
	require.GreaterOrEqual(t, end, 0)
	event := rust[start : start+end]
	for _, field := range requestLogFields {
		assert.Contains(t, event, field,
			"server_pal's access event no longer carries a %q field; a cross-service "+
				"log query silently misses this rail", field)
	}
}
