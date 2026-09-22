package stats

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"time"
)

// HubEventKey is one row of the per-day games_hub rollup (#1571): one
// event, counted under the values it carries. The columns are the
// event's own fields, not a shape imposed on them — an event that has no
// variant leaves it empty because it has none, and a reader filtering on
// `event` first never sees a column the event does not fill.
//
// `players` carries three things — a table's seats, the seats a finished
// game still held, and a room's size — told apart by `event`, which is
// in the key. They are never compared across events, and a reader that
// filters on `event` first never has to. What they do not share is a
// range, which is why the cap is the event's and not the column's.
//
// Room is deliberately absent. It is on every line in S3, where a reader
// asking what one evening looked like can pair created with closed and
// started with finished, but an aggregate keyed by it would be one row
// per room per day forever and would answer no question this serves.
//
// Every value is games_hub's own vocabulary (game_events.h; otel_contract
// pins the two spellings together), and a word this build does not know
// collapses to "other" so the line is still counted — the same rule as an
// unknown agent — and drift shows up as an "other" row rather than as
// missing events.
type HubEventKey struct {
	Date    string
	Event   string
	Variant string
	Surface string
	Outcome string
	Players int
}

const (
	// The largest count each event can carry and still key a row of its
	// own. `players` is not one quantity: game_started and game_finished
	// carry a table's seats, which the engine caps at four, while
	// room_joined and chat_message carry the room's size, which nothing
	// caps — a room hosts several tables at once, plus its chat and its
	// world, so six people in one is an ordinary evening and not drift.
	// One bound over all four would either lose real room sizes or stop
	// bounding the table.
	hubMaxSeats   = 4
	hubMaxMembers = 16
)

var (
	// Every event this reader knows, spelled once — this map is what
	// gates ConsumeGameEvents, so it is what otel_contract pins against
	// games_hub's own names. An event missing here is skipped outright
	// rather than counted as "other": there is no row shape for it.
	hubEvents = map[string]bool{
		"room_created": true, "room_joined": true, "room_closed": true,
		"chat_message": true, "geometry_changed": true,
		"game_started": true, "game_finished": true,
	}
	hubVariants = map[string]bool{"golf": true, "castle": true}
	hubOutcomes = map[string]bool{"completed": true, "abandoned": true}
	hubSurfaces = map[string]bool{"plane": true, "sphere": true, "glasshouse": true}
)

// hubEventLine is the slice of a games_hub event this reads. The hub
// writes exactly these keys and nothing else, so anything absent is a
// line from another writer or another build.
type hubEventLine struct {
	Timestamp int64  `json:"ts"` // epoch millis, as event_log's caller stamps it
	Event     string `json:"event"`
	Variant   string `json:"variant"`
	Surface   string `json:"surface"`
	Outcome   string `json:"outcome"`
	Players   int    `json:"players"`
}

// The day a line belongs to: its own timestamp, in UTC, or the object's
// date for a line without one.
func (l *hubEventLine) date(objectDate string) string {
	if l.Timestamp <= 0 {
		return objectDate
	}
	return time.UnixMilli(l.Timestamp).UTC().Format("2006-01-02")
}

// What this event's count is bounded by: a table's seats for the two
// that name a game, the room's size for the two that name a room.
func hubPlayersCap(event string) int {
	if event == "game_started" || event == "game_finished" {
		return hubMaxSeats
	}
	return hubMaxMembers
}

// A count this build is willing to key a row on: the count itself when
// it is one this event could carry, and playersOverCap when it is not.
// Never clamped to the cap — a clamped count reads as a real one and
// would be averaged as if it were, where a value that cannot be a count
// is loud.
func hubPlayers(event string, count int) int {
	if count < 0 || count > hubPlayersCap(event) {
		return playersOverCap
	}
	return count
}

// A count past what its event could carry, kept as a row rather than
// dropped so the drift is visible. Deliberately not a count: it must
// not be summed or averaged with the rows around it.
const playersOverCap = -1

// ConsumeGameEvents aggregates one object's worth of games_hub event
// lines into the rollup, each line under its own day, objectDate for a
// line without a timestamp. Lines that are not hub events are skipped
// and counted — same contract as Consume.
func (r *Rollup) ConsumeGameEvents(reader io.Reader, objectDate string) (skipped int, err error) {
	scanner := bufio.NewScanner(reader)
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	for scanner.Scan() {
		line := scanner.Bytes()
		if len(line) == 0 {
			continue
		}
		var parsed hubEventLine
		if json.Unmarshal(line, &parsed) != nil || !hubEvents[parsed.Event] {
			skipped++
			continue
		}
		r.HubEvents[HubEventKey{
			Date:    parsed.date(objectDate),
			Event:   parsed.Event,
			Variant: hubWord(hubVariants, parsed.Variant),
			Surface: hubWord(hubSurfaces, parsed.Surface),
			Outcome: hubWord(hubOutcomes, parsed.Outcome),
			Players: hubPlayers(parsed.Event, parsed.Players),
		}]++
	}
	if err := scanner.Err(); err != nil {
		return skipped, fmt.Errorf("reading hub event lines: %w", err)
	}
	return skipped, nil
}

// A word this build knows, "" for the events that carry none of that
// field, and "other" for one it does not — so a vocabulary the hub grows
// past this reader shows up rather than vanishing into a known bucket.
func hubWord(vocabulary map[string]bool, value string) string {
	if value == "" || vocabulary[value] {
		return value
	}
	return otherValue
}
