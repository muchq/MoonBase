package otel_contract

import (
	"regexp"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// http_server_requests_by_caller is recorded on the C++ and Rust rails, the
// two that serve compose-internal calls with no service-local caller of their
// own (one_d4 on the Java rail tags its callers in its query events). One
// series per caller only means one thing if both rails spell the callers, and
// describe the instrument, identically.

func TestCallerVocabularyAgreesAcrossRails(t *testing.T) {
	aura := codeLines(t, "../aura/middleware.h", "kInternalCallers")
	pal := codeLines(t, "../server_pal/src/lib.rs", "INTERNAL_CALLERS")

	assert.Equal(t,
		quotedListFrom(t, aura, "aura/middleware.h", regexp.MustCompile(`kInternalCallers\[\] = \{([^}]*)\}`)),
		quotedListFrom(t, pal, "server_pal", regexp.MustCompile(`INTERNAL_CALLERS: \[&str; \d+\] = \[([^\]]*)\]`)),
		"the rails name internal callers differently; the same caller would land in two series")

	for _, word := range []struct{ aura, pal, want string }{
		{`kEdgeCaller\[\] = "([a-z_]+)"`, `EDGE_CALLER: &str = "([a-z_]+)"`, "edge"},
		{`kOtherCaller\[\] = "([a-z_]+)"`, `OTHER_CALLER: &str = "([a-z_]+)"`, "other"},
	} {
		a := regexp.MustCompile(word.aura).FindSubmatch(aura)
		p := regexp.MustCompile(word.pal).FindSubmatch(pal)
		require.NotNil(t, a, "aura declares no %q caller", word.want)
		require.NotNil(t, p, "server_pal declares no %q caller", word.want)
		assert.Equal(t, word.want, string(a[1]))
		assert.Equal(t, word.want, string(p[1]))
	}
}

func TestCallerInstrumentIsDescribedIdenticallyOnBothRails(t *testing.T) {
	futility := codeLines(t, "../futility/otel/http_instrument_descriptions.h", "kHttpInstrumentDescriptions")
	pal := codeLines(t, "../server_pal/src/lib.rs", "http_server_requests_by_caller")

	c := regexp.MustCompile(`\{"http_server_requests_by_caller",\s*"([^"]*)"\}`).FindSubmatch(futility)
	r := regexp.MustCompile(`\("http_server_requests_by_caller"\)\s*\n\s*\.with_description\("([^"]*)"\)`).FindSubmatch(pal)
	require.NotNil(t, c, "futility declares no description for http_server_requests_by_caller")
	require.NotNil(t, r, "server_pal declares no description for http_server_requests_by_caller")
	assert.Equal(t, string(c[1]), string(r[1]),
		"the collector keeps the first description it sees and logs a conflict for every other")
}
