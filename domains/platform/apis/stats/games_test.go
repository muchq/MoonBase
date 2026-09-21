package stats

import (
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// One room's evening, as games_hub writes it (#1571).
const hubEvening = `
{"ts":1789998000000,"event":"room_created","room":"AB12CD","surface":"plane"}
{"ts":1789998001000,"event":"room_joined","room":"AB12CD","players":2}
{"ts":1789998002000,"event":"geometry_changed","room":"AB12CD","surface":"glasshouse"}
{"ts":1789998003000,"event":"chat_message","room":"AB12CD","players":2}
{"ts":1789998004000,"event":"game_started","room":"AB12CD","variant":"golf","players":2}
{"ts":1789998005000,"event":"game_finished","room":"AB12CD","variant":"golf","outcome":"completed","players":2}
{"ts":1789998006000,"event":"room_closed","room":"AB12CD"}
`

func hubRollup(t *testing.T, lines, objectDate string) (*Rollup, int) {
	t.Helper()
	rollup := NewRollup()
	skipped, err := rollup.ConsumeGameEvents(strings.NewReader(lines), objectDate)
	require.NoError(t, err)
	return rollup, skipped
}

func TestAnEveningIsCountedEventByEvent(t *testing.T) {
	rollup, skipped := hubRollup(t, hubEvening, "2026-09-21")
	assert.Zero(t, skipped)

	// Each event under its own fields, and nothing under fields it does
	// not carry: a room_closed has no variant, an outcome or a count.
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "room_created", Surface: "plane"}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "room_joined", Players: 2}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "geometry_changed", Surface: "glasshouse"}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "chat_message", Players: 2}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "game_started", Variant: "golf", Players: 2}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "game_finished", Variant: "golf",
		Outcome: "completed", Players: 2}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "room_closed"}])
	assert.Len(t, rollup.HubEvents, 7)
}

// The room is on every line and on no row: an aggregate keyed by it
// would be one row per room per day forever.
func TestTheRoomIsNotAKeyOfAnyRow(t *testing.T) {
	two := `{"ts":1789998000000,"event":"room_created","room":"AAAAAA","surface":"plane"}
{"ts":1789998001000,"event":"room_created","room":"BBBBBB","surface":"plane"}`
	rollup, skipped := hubRollup(t, two, "2026-09-21")
	assert.Zero(t, skipped)

	assert.Len(t, rollup.HubEvents, 1)
	assert.Equal(t, int64(2), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "room_created", Surface: "plane"}])
}

// A line's own timestamp dates it, so an hour's file that spans midnight
// puts each line on the day it happened.
func TestEachLineIsDatedByItsOwnTimestamp(t *testing.T) {
	// 23:59 on the 20th and 00:01 on the 21st, both in the hour's file
	// that rolled after midnight.
	across := `{"ts":1789948740000,"event":"room_closed","room":"AAAAAA"}
{"ts":1789948860000,"event":"room_closed","room":"BBBBBB"}`
	rollup, _ := hubRollup(t, across, "2026-09-22")

	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{Date: "2026-09-20", Event: "room_closed"}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{Date: "2026-09-21", Event: "room_closed"}])
}

func TestALineWithNoTimestampTakesTheObjectsDay(t *testing.T) {
	rollup, _ := hubRollup(t, `{"event":"room_closed","room":"AAAAAA"}`, "2026-09-21")
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{Date: "2026-09-21", Event: "room_closed"}])
}

// A word this build does not know is still counted, under "other", so
// the hub growing its vocabulary shows up as a row rather than as
// events that quietly stop arriving.
func TestAWordThisBuildDoesNotKnowIsCountedAsOther(t *testing.T) {
	unknown := `{"ts":1789998000000,"event":"game_finished","room":"A","variant":"hearts","outcome":"conceded","players":3}
{"ts":1789998000000,"event":"room_created","room":"A","surface":"torus"}`
	rollup, skipped := hubRollup(t, unknown, "2026-09-21")
	assert.Zero(t, skipped, "an unknown word is not an unreadable line")

	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "game_finished", Variant: "other",
		Outcome: "other", Players: 3}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "room_created", Surface: "other"}])
}

// An event name this build does not know is a different matter: there is
// no row shape for it, so it is skipped and the count says so.
func TestAnUnknownEventIsSkippedRatherThanCounted(t *testing.T) {
	rollup, skipped := hubRollup(t,
		`{"ts":1789998000000,"event":"player_waved","room":"A"}`, "2026-09-21")
	assert.Equal(t, 1, skipped)
	assert.Empty(t, rollup.HubEvents)
}

func TestLinesThatAreNotHubEventsAreSkipped(t *testing.T) {
	mixed := `not json at all
{"level":"info","msg":"hub restored 0 rooms"}
{"ts":1789998000000,"event":"room_closed","room":"AAAAAA"}
`
	rollup, skipped := hubRollup(t, mixed, "2026-09-21")
	assert.Equal(t, 2, skipped)
	assert.Len(t, rollup.HubEvents, 1)
}

// A seat count past a table's size is not a table. It is kept as a row
// rather than dropped, so a hub that starts dealing five is visible.
func TestASeatCountNoTableCouldHaveIsKeptApart(t *testing.T) {
	odd := `{"ts":1789998000000,"event":"game_started","room":"A","variant":"golf","players":9}
{"ts":1789998000000,"event":"game_started","room":"A","variant":"golf","players":4}`
	rollup, skipped := hubRollup(t, odd, "2026-09-21")
	assert.Zero(t, skipped)

	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "game_started", Variant: "golf", Players: otherSeats}])
	assert.Equal(t, int64(1), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "game_started", Variant: "golf", Players: 4}])
}

// The same shape twice is one row with two events, which is the whole
// point of an aggregate.
func TestTheSameEventTwiceIsOneRow(t *testing.T) {
	rollup, _ := hubRollup(t, hubEvening+hubEvening, "2026-09-21")
	assert.Len(t, rollup.HubEvents, 7)
	assert.Equal(t, int64(2), rollup.HubEvents[HubEventKey{
		Date: "2026-09-21", Event: "game_started", Variant: "golf", Players: 2}])
}
