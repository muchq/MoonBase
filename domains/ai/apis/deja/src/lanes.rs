//! One lane per sequence: the last few token ids that sequence carried,
//! in a table bounded by evicting the lane idle longest. What keys a lane
//! is the source's business — a client's address for caddy, a room for
//! the hub — and it stays here; a lane's number is all that leaves.
//!
//! The bound is **per source**, not over the table. Caddy is orders of
//! magnitude busier than the hub, so one shared quota let its churn take
//! the lane of any room quiet for longer than the rest of the stream
//! needs to touch every slot — which is every gap a room's evening is
//! made of. The room came back with an empty window, and the sequence
//! this source exists to model never formed.

use std::collections::{HashMap, VecDeque};

pub const WINDOW: usize = 8;
/// Caddy's share: every client the site sees between evictions.
pub const DEFAULT_LANES: usize = 4096;
/// The hub's. Not a count of open rooms: like caddy's, it is an LRU over
/// the keys touched, so a room that closed holds its slot until 512 more
/// rooms have been seen. Sized so an evening's rooms all keep theirs.
pub const HUB_LANES: usize = 512;

/// Which stream a lane belongs to, and how many lanes that stream gets.
/// A source's share is its own: it can evict its own idle lanes and no
/// others.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Source {
    Caddy,
    Hub,
}

impl Source {
    fn slot(self) -> usize {
        match self {
            Source::Caddy => 0,
            Source::Hub => 1,
        }
    }
}

pub struct Lanes {
    slots: Vec<Slot>,
    by_key: HashMap<(Source, String), usize>,
    /// Caddy's share, and the hub's as a fraction of it — so `Lanes::new`
    /// keeps taking one number and caddy's behaviour is what it was.
    caps: [usize; 2],
    counts: [usize; 2],
    clock: u64,
}

struct Slot {
    source: Source,
    key: String,
    window: VecDeque<u16>,
    used: u64,
}

impl Lanes {
    /// `cap` is caddy's share; the hub takes its own default beside it.
    pub fn new(cap: usize) -> Self {
        Self::with_shares(cap, HUB_LANES)
    }

    /// The two shares spelled out, which is what the tests want: scaling
    /// the hub's from caddy's would hand a small table a share of one and
    /// make an eviction test prove the wrong thing.
    pub fn with_shares(caddy: usize, hub: usize) -> Self {
        assert!(caddy > 0 && hub > 0, "at least one lane each");
        Self {
            slots: Vec::new(),
            by_key: HashMap::new(),
            caps: [caddy, hub],
            counts: [0, 0],
            clock: 0,
        }
    }

    /// This key's lane, opened if it has none: a new slot while the
    /// source is under its share, otherwise the slot of **that source's**
    /// lane idle longest, whose window is forgotten with it.
    pub fn touch(&mut self, source: Source, key: &str) -> usize {
        self.clock += 1;
        let owned = (source, key.to_string());
        if let Some(&lane) = self.by_key.get(&owned) {
            self.slots[lane].used = self.clock;
            return lane;
        }
        let share = source.slot();
        let lane = if self.counts[share] < self.caps[share] {
            self.slots.push(Slot {
                source,
                key: key.to_string(),
                window: VecDeque::with_capacity(WINDOW),
                used: self.clock,
            });
            self.counts[share] += 1;
            self.slots.len() - 1
        } else {
            let lane = (0..self.slots.len())
                .filter(|&lane| self.slots[lane].source == source)
                .min_by_key(|&lane| self.slots[lane].used)
                .expect("a source at its share has a lane to give up");
            let slot = &mut self.slots[lane];
            self.by_key.remove(&(slot.source, slot.key.clone()));
            slot.key = key.to_string();
            slot.window.clear();
            slot.used = self.clock;
            lane
        };
        self.by_key.insert(owned, lane);
        lane
    }

    pub fn window(&self, lane: usize) -> &VecDeque<u16> {
        &self.slots[lane].window
    }

    pub fn push(&mut self, lane: usize, token: u16) {
        let window = &mut self.slots[lane].window;
        if window.len() == WINDOW {
            window.pop_front();
        }
        window.push_back(token);
    }

    #[cfg(test)]
    fn len(&self) -> usize {
        self.slots.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_client_keeps_its_lane_and_its_window() {
        let mut lanes = Lanes::new(4);
        let a = lanes.touch(Source::Caddy, "1.1.1.1");
        lanes.push(a, 7);
        let b = lanes.touch(Source::Caddy, "2.2.2.2");
        assert_ne!(a, b);
        assert_eq!(lanes.touch(Source::Caddy, "1.1.1.1"), a);
        assert_eq!(lanes.window(a).iter().copied().collect::<Vec<_>>(), [7]);
        assert!(lanes.window(b).is_empty());
    }

    #[test]
    fn the_window_keeps_the_last_eight() {
        let mut lanes = Lanes::new(1);
        let a = lanes.touch(Source::Caddy, "c");
        for token in 0..10 {
            lanes.push(a, token);
        }
        assert_eq!(
            lanes.window(a).iter().copied().collect::<Vec<_>>(),
            (2..10).collect::<Vec<u16>>()
        );
    }

    // Each source evicts its own. Caddy is orders of magnitude busier
    // than the hub, and one shared table let its churn take the lane of
    // any room quiet for longer than it takes the rest of the stream to
    // touch every slot — which is every gap a room's evening is made of.
    // The room came back with an empty window and a `<bos>` context, so
    // the sequence this source exists to model never formed.
    #[test]
    fn one_sources_churn_cannot_take_another_sources_lane() {
        let mut lanes = Lanes::with_shares(2, 1);
        let room = lanes.touch(Source::Hub, "ABC123");
        lanes.push(room, 7);

        // Caddy fills and overruns its whole share, many times over.
        for client in 0..50 {
            lanes.touch(Source::Caddy, &format!("10.0.0.{client}"));
        }

        assert_eq!(
            lanes.touch(Source::Hub, "ABC123"),
            room,
            "the room kept its lane through caddy's churn"
        );
        assert_eq!(
            lanes.window(room).iter().copied().collect::<Vec<_>>(),
            [7],
            "and kept the sequence in it"
        );
    }

    // The quota is per source, so a source cannot grow past its own
    // share either: the hub evicts hub lanes when it has filled its own.
    #[test]
    fn a_source_at_its_share_evicts_its_own_idle_longest() {
        let mut lanes = Lanes::with_shares(1, 2);
        lanes.touch(Source::Hub, "AAA111");
        let second = lanes.touch(Source::Hub, "BBB222");
        lanes.touch(Source::Hub, "AAA111");
        let third = lanes.touch(Source::Hub, "CCC333");
        assert_eq!(third, second, "the idle-longest hub lane went");
        assert_eq!(lanes.len(), 2, "and no third slot was minted");
    }

    #[test]
    fn a_full_table_evicts_the_client_idle_longest() {
        let mut lanes = Lanes::new(2);
        let a = lanes.touch(Source::Caddy, "a");
        lanes.push(a, 1);
        let b = lanes.touch(Source::Caddy, "b");
        lanes.touch(Source::Caddy, "a"); // a is now the more recent of the two
        let c = lanes.touch(Source::Caddy, "c");
        assert_eq!(c, b, "c takes b's slot");
        assert!(lanes.window(c).is_empty(), "b's window went with it");
        assert_eq!(lanes.len(), 2);
        // b is gone; touching it again opens a fresh lane by evicting a.
        let b2 = lanes.touch(Source::Caddy, "b");
        assert_eq!(b2, a);
        assert!(lanes.window(b2).is_empty());
    }
}
