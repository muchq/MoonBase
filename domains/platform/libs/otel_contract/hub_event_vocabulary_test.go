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
	hubSurfaceH     = "../../../games/apis/games_hub/surface.h"
	hubHostedGameH  = "../../../games/apis/games_hub/hosted_game.h"
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

	// hubEvents = map[string]bool{hubRoomCreated: true, …} is what gates
	// ConsumeGameEvents, so it is what has to agree — a const declared
	// and never added to the map would pass a pin on the const block
	// while the event it names went on being skipped.
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
	for _, match := range regexp.MustCompile(`return "([a-z]+)";`).FindAllSubmatch(
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
