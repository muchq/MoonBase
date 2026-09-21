package otel_contract

import (
	"fmt"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The rolled-log filename contract (#1571). log_shipper uploads and then
// DELETES every file matching its rolledLog pattern, so a writer feeding
// it has two obligations and they pull in opposite directions: the file
// it is still appending to must not match, and the ones it has finished
// must. event_log is the C++ side of that, and it builds both names from
// two constants. This is the one place that says the two sides agree.
//
// Read as text — a Go test cannot link a C++ library, and it is the
// declaration that has to hold, not what some running process reports.

const (
	eventLogSource = "../event_log/event_log.cc"
	shipperSource  = "../../apps/log_shipper/shipper.go"
)

// strftime to Go's reference layout, for the pieces event_log's hour
// format is allowed to use. A directive added there without an entry
// here fails the conversion rather than passing silently.
var strftimeToGo = map[string]string{
	"%Y": "2006", "%m": "01", "%d": "02", "%H": "15",
}

func goLayoutOf(t *testing.T, strftime string) string {
	t.Helper()
	layout := strftime
	for directive, reference := range strftimeToGo {
		layout = strings.ReplaceAll(layout, directive, reference)
	}
	require.NotContains(t, layout, "%",
		"event_log's hour format uses a strftime directive this pin cannot render: %q", strftime)
	return layout
}

// The literal after `constexpr char <name>[] = ` in event_log.cc.
func cppConstant(t *testing.T, source []byte, name string) string {
	t.Helper()
	declaration := regexp.MustCompile(`constexpr char ` + name + `\[\] = "([^"]*)"`)
	match := declaration.FindSubmatch(source)
	require.NotNil(t, match, "no %s in %s; if it was renamed, rename it here too",
		name, eventLogSource)
	return string(match[1])
}

func shipperPattern(t *testing.T) *regexp.Regexp {
	t.Helper()
	source := codeLines(t, shipperSource, "rolledLog")
	declaration := regexp.MustCompile("var rolledLog = regexp.MustCompile\\(`([^`]*)`\\)")
	match := declaration.FindSubmatch(source)
	require.NotNil(t, match, "no rolledLog pattern in %s", shipperSource)
	pattern, err := regexp.Compile(string(match[1]))
	require.NoError(t, err)
	return pattern
}

func eventLogNames(t *testing.T, name string, when time.Time) (active, rolled string) {
	t.Helper()
	source := codeLines(t, eventLogSource, "kHourFormat")
	suffix := cppConstant(t, source, "kSuffix")
	layout := goLayoutOf(t, cppConstant(t, source, "kHourFormat"))
	return name + suffix, name + "-" + when.UTC().Format(layout) + suffix
}

func TestTheActiveEventLogIsNeverAFileTheShipperWouldTakeAway(t *testing.T) {
	rolledLog := shipperPattern(t)
	active, _ := eventLogNames(t, "game_events", time.Now())

	assert.False(t, rolledLog.MatchString(active),
		"%s matches log_shipper's rolledLog, so the shipper would upload and DELETE the file "+
			"games_hub is still appending to", active)
}

func TestEveryNameEventLogRollsIsOneTheShipperShips(t *testing.T) {
	rolledLog := shipperPattern(t)
	when := time.Date(2026, 9, 21, 13, 40, 0, 0, time.UTC)
	_, rolled := eventLogNames(t, "game_events", when)

	// The pattern's second group is the S3 partition the file lands
	// under, so a name that matches with the wrong date is still wrong.
	match := rolledLog.FindStringSubmatch(rolled)
	require.NotNil(t, match,
		"%s does not match log_shipper's rolledLog, so rolled game events would sit on the "+
			"host forever, counted only in the shipper's `skipped`", rolled)
	assert.Equal(t, "2026-09-21", match[2], "%s would ship under the wrong dt= partition", rolled)

	// RollLocked falls back to `<stem>-<n>` when the hour's name is
	// taken, which has to ship the same way and under the same date.
	disambiguated := strings.TrimSuffix(rolled, ".log") + "-1.log"
	collided := rolledLog.FindStringSubmatch(disambiguated)
	require.NotNil(t, collided, "%s does not match log_shipper's rolledLog", disambiguated)
	assert.Equal(t, "2026-09-21", collided[2])
}

// One directory, one source label, and the shipper partitions by that
// label alone: two writers in it would interleave under one name.
func TestTheHourFormatIsTheOneCaddyAndLogbackAlreadyRollUnder(t *testing.T) {
	source := codeLines(t, eventLogSource, "kHourFormat")
	layout := goLayoutOf(t, cppConstant(t, source, "kHourFormat"))
	when := time.Date(2026, 9, 21, 13, 0, 0, 0, time.UTC)

	// logback's fileNamePattern for one_d4's query events, which the
	// shipper has shipped since #1465: yyyy-MM-dd'T'HH.
	assert.Equal(t, "2026-09-21T13", when.Format(layout),
		fmt.Sprintf("event_log rolls under a different stamp than %s", shipperSource))
}
