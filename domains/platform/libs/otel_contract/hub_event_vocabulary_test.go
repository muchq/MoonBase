package otel_contract

import (
	"os"
	"regexp"
	"sort"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The games_hub event vocabulary (#1571): the hub writes event names,
// variants, outcomes and surfaces from game_events.h, and the stats
// reader keys its rollup on the same words, collapsing any it does not
// know to "other". The two live in different languages; this is the one
// place that says they are the same words. A word added on the C++ side
// without its Go twin would quietly become an "other" row, which reads
// as a vocabulary that drifted rather than one that grew.
//
// The event *names* are the sharper half: an unknown value still counts
// its line, but an unknown event name is skipped entirely, so a hub that
// grows an eighth event would go unrecorded until this fails.

const (
	hubEventsHeader = "../../../games/apis/games_hub/game_events.h"
	hubEventsCc     = "../../../games/apis/games_hub/game_events.cc"
	hubSurfaceH     = "../../../games/apis/games_hub/surface.h"
	hubHostedGameH  = "../../../games/apis/games_hub/hosted_game.h"
	hubGolfHubCc    = "../../../games/apis/games_hub/golf_hub.cc"
	statsGames      = "../../apis/stats/games.go"
)

// The quoted values of `inline constexpr std::string_view <prefix>… = "…"`.
func cppConstants(t *testing.T, source []byte, prefix string) []string {
	t.Helper()
	declaration := regexp.MustCompile(
		`inline constexpr std::string_view ` + prefix + `\w+ = "([^"]+)"`)
	var values []string
	for _, match := range declaration.FindAllSubmatch(source, -1) {
		values = append(values, string(match[1]))
	}
	require.NotEmpty(t, values, "no %s* constants in %s", prefix, hubEventsHeader)
	sort.Strings(values)
	return values
}

// The keys of a `name = map[string]bool{"a": true, …}` declaration, or of
// a const block's quoted values.
func goWords(t *testing.T, source []byte, pattern *regexp.Regexp, what string) []string {
	t.Helper()
	block := pattern.FindSubmatch(source)
	require.NotNil(t, block, "no %s in %s; if it was renamed, rename it here too", what, statsGames)
	var values []string
	for _, match := range regexp.MustCompile(`"([^"]+)"`).FindAllSubmatch(block[1], -1) {
		values = append(values, string(match[1]))
	}
	require.NotEmpty(t, values, "parsed an empty %s out of %s", what, statsGames)
	sort.Strings(values)
	return values
}

func TestHubEventNamesAgreeBetweenGamesHubAndStats(t *testing.T) {
	header, err := os.ReadFile(hubEventsHeader)
	require.NoError(t, err)
	reader, err := os.ReadFile(statsGames)
	require.NoError(t, err)

	// hubEvents is the map that gates ConsumeGameEvents, so it is what has
	// to agree. The names are spelled into it as literals for that reason:
	// a constant declared beside the map and never added to it would pass
	// a pin on the constant while the event it named went on being
	// skipped.
	fromGo := goWords(t, reader,
		regexp.MustCompile(`(?s)hubEvents = map\[string\]bool\{(.*?)\n\t\}`), "hubEvents")
	assert.Equal(t, cppConstants(t, header, "kEvent"), fromGo,
		"the hub writes event names stats does not read, or the other way round. An event "+
			"name this reader does not know is skipped outright, not counted as \"other\": "+
			"the whole event would go unrecorded.")
}

func TestHubEventValuesAgreeBetweenGamesHubAndStats(t *testing.T) {
	header, err := os.ReadFile(hubEventsHeader)
	require.NoError(t, err)
	surface, err := os.ReadFile(hubSurfaceH)
	require.NoError(t, err)
	reader, err := os.ReadFile(statsGames)
	require.NoError(t, err)

	// The outcomes are the kOutcome* constants beside the event names;
	// the surfaces are SurfaceKindName's three, spelled in the switch
	// that names them.
	fromHeader := cppConstants(t, header, "kOutcome")
	assert.Equal(t, fromHeader, goWords(t, reader,
		regexp.MustCompile(`hubOutcomes = map\[string\]bool\{([^}]*)\}`), "hubOutcomes"),
		"an ending the hub can write that stats would count as \"other\"")

	// GameKindName: return kind == GameKind::kCastle ? "castle" : "golf";
	hosted, err := os.ReadFile(hubHostedGameH)
	require.NoError(t, err)
	kindName := regexp.MustCompile(`(?s)GameKindName\(GameKind kind\) \{(.*?)\n\}`).
		FindSubmatch(hosted)
	require.NotNil(t, kindName, "no GameKindName in %s", hubHostedGameH)
	var fromKinds []string
	for _, match := range regexp.MustCompile(`"([a-z_]+)"`).FindAllSubmatch(kindName[1], -1) {
		fromKinds = append(fromKinds, string(match[1]))
	}
	sort.Strings(fromKinds)
	assert.Equal(t, fromKinds, goWords(t, reader,
		regexp.MustCompile(`hubVariants = map\[string\]bool\{([^}]*)\}`), "hubVariants"),
		"a game the hub can host that stats would count as \"other\"")

	// SurfaceKindName: return "sphere"; return "glasshouse"; return "plane";
	var fromSurface []string
	for _, match := range regexp.MustCompile(`return "([a-z_]+)";`).FindAllSubmatch(
		surfaceKindNameBody(t, surface), -1) {
		fromSurface = append(fromSurface, string(match[1]))
	}
	sort.Strings(fromSurface)
	assert.Equal(t, fromSurface, goWords(t, reader,
		regexp.MustCompile(`hubSurfaces = map\[string\]bool\{([^}]*)\}`), "hubSurfaces"),
		"a shape a room can be that stats would count as \"other\"")
}

// The body of SurfaceKindName, which is where the three spellings live —
// SurfaceJson renders them through it, so this is the declaration site.
func surfaceKindNameBody(t *testing.T, source []byte) []byte {
	t.Helper()
	body := regexp.MustCompile(`(?s)SurfaceKindName\(const Surface& surface\) \{(.*?)\n\}`).
		FindSubmatch(source)
	require.NotNil(t, body, "no SurfaceKindName in %s", hubSurfaceH)
	return body[1]
}

// The JSON keys are the other half of the vocabulary, and the quieter
// one: an event name the reader does not know is skipped loudly, but a
// renamed field simply arrives as its type's zero value, so a renamed
// `players` would read as a table of nobody rather than as an error.
func TestHubEventFieldNamesAgreeBetweenGamesHubAndStats(t *testing.T) {
	writer, err := os.ReadFile(hubEventsCc)
	require.NoError(t, err)
	reader, err := os.ReadFile(statsGames)
	require.NoError(t, err)

	// Every `"key":` the line builders format, in Line's own fixed prefix
	// and in each event's fields.
	written := map[string]bool{}
	for _, match := range regexp.MustCompile(`"(\w+)":`).FindAllSubmatch(writer, -1) {
		written[string(match[1])] = true
	}
	require.NotEmpty(t, written, "no JSON keys in %s", hubEventsCc)

	// hubEventLine's tags, plus the one field the reader leaves on the
	// floor on purpose: room is on every line in S3 and in no rollup row,
	// because an aggregate keyed by it would be one row per room forever.
	read := map[string]bool{"room": true}
	line := regexp.MustCompile(`(?s)type hubEventLine struct \{(.*?)\n\}`).FindSubmatch(reader)
	require.NotNil(t, line, "no hubEventLine in %s", statsGames)
	for _, match := range regexp.MustCompile("`json:\"(\\w+)\"`").FindAllSubmatch(line[1], -1) {
		read[string(match[1])] = true
	}

	for key := range written {
		assert.True(t, read[key],
			"games_hub writes %q and the stats reader has no field for it: the column would "+
				"arrive as a zero rather than as an error", key)
	}
}

// The seat cap the reader bounds a table by is the engine's, and the
// engine's is a constant in an anonymous namespace rather than anything
// exported — so this is read as text like the rest of the contract. Raise
// the engine to five seats without raising the reader and every five-seat
// table becomes a players of -1: a row that says "past the bound" about a
// perfectly ordinary game.
func TestTheTableCapAgreesBetweenGamesHubAndStats(t *testing.T) {
	hub, err := os.ReadFile(hubGolfHubCc)
	require.NoError(t, err)
	reader, err := os.ReadFile(statsGames)
	require.NoError(t, err)

	seats := regexp.MustCompile(`constexpr std::size_t kMaxSeats = (\d+);`).FindSubmatch(hub)
	require.NotNil(t, seats, "no kMaxSeats in %s", hubGolfHubCc)
	bound := regexp.MustCompile(`hubMaxSeats\s+= (\d+)`).FindSubmatch(reader)
	require.NotNil(t, bound, "no hubMaxSeats in %s", statsGames)
	assert.Equal(t, string(seats[1]), string(bound[1]),
		"the hub seats a table differently than the reader will count one")
}

// hubMaxMembers has no twin on purpose, and this says so rather than
// leaving the next reader to look for one: nothing caps a room, so 16 is
// the reader's own ceiling on how many rows one room shape may mint.
func TestTheRoomCeilingIsTheReadersAlone(t *testing.T) {
	hub, err := os.ReadFile(hubGolfHubCc)
	require.NoError(t, err)
	assert.NotRegexp(t, `kMaxMembers|kMaxRoomSize`, string(hub),
		"the hub grew a room cap; hubMaxMembers should be pinned to it rather than "+
			"documented as a reader-side ceiling")
}
