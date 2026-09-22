//! games_hub's domain events as deja tokens (#1572): the hub writes one
//! JSON line per thing that happened in a room, and this reads that line
//! into the three things the engine wants — a bounded token, the lane its
//! sequence belongs to, and when.
//!
//! The room is the lane, which is why this source needed no lane-keying
//! decision: a room's evening already *is* a sequence. A client address
//! is what a caddy line has instead; neither leaves the lane table.
//!
//! With one exception the hub itself writes. `geometry_changed` is
//! tagged with the *world* it reshaped, and a player who is in no room
//! is in the plaza — so `hub:plaza` is one lane holding every lobby
//! reshape by everybody, not one session's sequence. It is kept rather
//! than dropped: the plaza is a single shared world, so which shapes
//! people reach for in it is a real question, and it is the room-shaped
//! lane it is not. Nothing else the hub writes can carry it.

use serde::Deserialize;

use crate::engine::Observation;

/// Bytes a room id may run to as a lane key.
const MAX_ROOM: usize = 64;

/// The slice of a hub event line this reads. Every other field the hub
/// writes is deliberately absent: a room's size is a count, not a word to
/// build a grammar from.
#[derive(Deserialize)]
struct HubLine {
    ts: i64,
    event: String,
    room: String,
    #[serde(default)]
    variant: String,
    #[serde(default)]
    surface: String,
    #[serde(default)]
    outcome: String,
    #[serde(default)]
    players: i64,
}

/// The words this build knows. Every one is bounded here rather than
/// trusted from the file: the hub is another process and may ship ahead
/// of this one, so a word this reader does not know becomes `other` and
/// the drift is a token instead of a vocabulary growing behind its back.
/// otel_contract pins these four lists against game_events.h.
const EVENTS: [&str; 7] = [
    "room_created",
    "room_joined",
    "room_closed",
    "chat_message",
    "geometry_changed",
    "game_started",
    "game_finished",
];
const VARIANTS: [&str; 2] = ["golf", "castle"];
const SURFACES: [&str; 3] = ["plane", "sphere", "glasshouse"];
const OUTCOMES: [&str; 2] = ["completed", "abandoned"];
/// The seats the engine deals a table, from golf_hub's kMaxSeats.
const MAX_SEATS: i64 = 4;
const OTHER: &str = "other";

fn known(vocabulary: &[&'static str], word: &str) -> &'static str {
    vocabulary
        .iter()
        .copied()
        .find(|&candidate| candidate == word)
        .unwrap_or(OTHER)
}

/// A table's seats as a word. Bounded by what a table can hold, so this
/// is a handful of tokens and not a number line.
fn seats(players: i64) -> String {
    if (1..=MAX_SEATS).contains(&players) {
        players.to_string()
    } else {
        OTHER.to_string()
    }
}

/// The room as a lane key: namespaced, because the lane table is shared
/// with caddy's clients and a room id must never land in an address's
/// window; reduced and cut, because this reads a file rather than trusting
/// one.
fn lane_key(room: &str) -> String {
    let mut key = String::from("hub:");
    for c in room.chars().take(MAX_ROOM) {
        key.push(if c.is_ascii_alphanumeric() || c == '_' || c == '-' {
            c
        } else {
            '_'
        });
    }
    key
}

/// A hub event as one bounded token, or `None` for a line that is not a
/// hub event at all.
pub fn observation(line: &[u8]) -> Option<Observation> {
    let parsed: HubLine = serde_json::from_slice(line).ok()?;
    if parsed.room.is_empty() {
        return None;
    }
    let event = known(&EVENTS, &parsed.event);
    let token = match event {
        "room_created" | "geometry_changed" => {
            format!("hub {event} {}", known(&SURFACES, &parsed.surface))
        }
        "game_started" => format!(
            "hub {event} {} {}",
            known(&VARIANTS, &parsed.variant),
            seats(parsed.players)
        ),
        "game_finished" => format!(
            "hub {event} {} {}",
            known(&VARIANTS, &parsed.variant),
            known(&OUTCOMES, &parsed.outcome)
        ),
        // room_joined, chat_message, room_closed — and an event name this
        // build does not know, which is still an event.
        _ => format!("hub {event}"),
    };
    Some(Observation {
        token,
        lane_key: lane_key(&parsed.room),
        // Millis on the wire, seconds in an event, like every other source.
        ts: parsed.ts as f64 / 1000.0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn obs(json: &str) -> Observation {
        observation(json.as_bytes()).expect("a hub event")
    }

    #[test]
    fn each_event_is_its_name_and_the_words_it_carries() {
        assert_eq!(
            obs(r#"{"ts":1,"event":"room_created","room":"abc","surface":"sphere"}"#).token,
            "hub room_created sphere"
        );
        assert_eq!(
            obs(r#"{"ts":1,"event":"geometry_changed","room":"abc","surface":"glasshouse"}"#).token,
            "hub geometry_changed glasshouse"
        );
        assert_eq!(
            obs(r#"{"ts":1,"event":"game_started","room":"abc","variant":"golf","players":4}"#)
                .token,
            "hub game_started golf 4"
        );
        assert_eq!(
            obs(r#"{"ts":1,"event":"game_finished","room":"abc","variant":"castle","outcome":"abandoned","players":1}"#)
                .token,
            "hub game_finished castle abandoned"
        );
    }

    // A room's size is a count, and counts do not belong in a vocabulary:
    // a busy room would mint a token per size and crowd out the grammar.
    // The sequence is the signal — that somebody joined, not how many.
    #[test]
    fn a_rooms_size_is_not_in_the_token() {
        assert_eq!(
            obs(r#"{"ts":1,"event":"room_joined","room":"abc","players":7}"#).token,
            "hub room_joined"
        );
        assert_eq!(
            obs(r#"{"ts":1,"event":"chat_message","room":"abc","players":16}"#).token,
            "hub chat_message"
        );
        assert_eq!(
            obs(r#"{"ts":1,"event":"room_closed","room":"abc"}"#).token,
            "hub room_closed"
        );
    }

    // A table's seats are bounded by the engine at four, so they are a
    // word rather than a count — how many sat down is the difference
    // between two people and a full table.
    #[test]
    fn a_table_past_its_seats_is_a_word_not_a_number() {
        assert_eq!(
            obs(r#"{"ts":1,"event":"game_started","room":"a","variant":"golf","players":9}"#).token,
            "hub game_started golf other"
        );
        assert_eq!(
            obs(r#"{"ts":1,"event":"game_started","room":"a","variant":"golf","players":-1}"#)
                .token,
            "hub game_started golf other"
        );
    }

    // Every factor is bounded here, not upstream: the hub is another
    // process and may ship ahead of this one. A word this build does not
    // know is "other", so drift is a token rather than a vocabulary a
    // release can grow without this reader agreeing.
    #[test]
    fn a_word_this_build_does_not_know_is_other() {
        assert_eq!(
            obs(r#"{"ts":1,"event":"room_created","room":"a","surface":"escher"}"#).token,
            "hub room_created other"
        );
        assert_eq!(
            obs(r#"{"ts":1,"event":"game_started","room":"a","variant":"bridge","players":2}"#)
                .token,
            "hub game_started other 2"
        );
        assert_eq!(
            obs(r#"{"ts":1,"event":"game_finished","room":"a","variant":"golf","outcome":"conceded","players":1}"#)
                .token,
            "hub game_finished golf other"
        );
    }

    // An event name this build does not know still counts, where stats
    // skips it: a per-day count has no row shape for an unknown event,
    // but a sequence model has a hole where the line should have been.
    #[test]
    fn an_event_this_build_does_not_know_is_still_an_event() {
        let o = obs(r#"{"ts":1,"event":"hand_played","room":"abc"}"#);
        assert_eq!(o.token, "hub other");
        assert_eq!(o.lane_key, "hub:abc");
    }

    // The plaza is the lobby, not a room, so this lane is every lobby
    // player's reshapes interleaved. Pinned because it is the one lane
    // this source has that is not one session's sequence.
    #[test]
    fn the_lobbys_reshapes_share_one_lane_that_is_not_a_room() {
        let o = obs(
            r#"{"ts":1,"event":"geometry_changed","room":"plaza","surface":"glasshouse"}"#,
        );
        assert_eq!(o.lane_key, "hub:plaza");
        assert_eq!(o.token, "hub geometry_changed glasshouse");
    }

    // The room is the lane. It is namespaced because the lane table is
    // shared with caddy's clients, and a room id and an address must
    // never land in the same window — twice over, since an address's
    // dots are not characters a room id may carry either.
    #[test]
    fn the_room_is_the_lane_and_cannot_collide_with_a_client() {
        assert_eq!(
            obs(r#"{"ts":1,"event":"room_closed","room":"1.2.3.4"}"#).lane_key,
            "hub:1_2_3_4"
        );
    }

    // The writer already reduces a room id to [A-Za-z0-9_-], and this
    // does it again rather than trusting a file on disk.
    #[test]
    fn a_room_id_is_reduced_again_here_and_bounded() {
        assert_eq!(
            obs(r#"{"ts":1,"event":"room_closed","room":"a b/../c-d_e"}"#).lane_key,
            "hub:a_b____c-d_e",
            "the two punctuation marks a room id may carry are kept"
        );
        let long = "z".repeat(200);
        let o = obs(&format!(r#"{{"ts":1,"event":"room_closed","room":"{long}"}}"#));
        assert_eq!(o.lane_key.len(), "hub:".len() + MAX_ROOM);
    }

    // Millis on the wire, seconds in an event, like every other source.
    #[test]
    fn the_timestamp_arrives_in_seconds() {
        assert_eq!(
            obs(r#"{"ts":1750000000123,"event":"room_closed","room":"a"}"#).ts,
            1_750_000_000.123
        );
    }

    #[test]
    fn a_line_that_is_not_a_hub_event_is_none() {
        assert!(observation(b"not json").is_none());
        assert!(observation(br#"{"level":"info","msg":"hello"}"#).is_none());
        assert!(observation(b"").is_none());
        // A hub event needs a room: it has no lane without one, and an
        // empty one is no more a lane than a missing one.
        assert!(observation(br#"{"ts":1,"event":"room_closed"}"#).is_none());
        assert!(observation(br#"{"ts":1,"event":"room_closed","room":""}"#).is_none());
    }
}
