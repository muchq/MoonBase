//! One lane per client: the last few token ids a client sent, in a table
//! bounded by evicting the client idle longest. The client's address is
//! the key and stays here; a lane's number is all that leaves.

use std::collections::{HashMap, VecDeque};

pub const WINDOW: usize = 8;
pub const DEFAULT_LANES: usize = 4096;

pub struct Lanes {
    slots: Vec<Slot>,
    by_client: HashMap<String, usize>,
    cap: usize,
    clock: u64,
}

struct Slot {
    client: String,
    window: VecDeque<u16>,
    used: u64,
}

impl Lanes {
    pub fn new(cap: usize) -> Self {
        assert!(cap > 0, "at least one lane");
        Self {
            slots: Vec::new(),
            by_client: HashMap::new(),
            cap,
            clock: 0,
        }
    }

    /// The client's lane, opened if it has none: the free slot if any,
    /// otherwise the slot of the client idle longest, whose window is
    /// forgotten with it.
    pub fn touch(&mut self, client: &str) -> usize {
        self.clock += 1;
        if let Some(&lane) = self.by_client.get(client) {
            self.slots[lane].used = self.clock;
            return lane;
        }
        let lane = if self.slots.len() < self.cap {
            self.slots.push(Slot {
                client: client.to_string(),
                window: VecDeque::with_capacity(WINDOW),
                used: self.clock,
            });
            self.slots.len() - 1
        } else {
            let lane = (0..self.slots.len())
                .min_by_key(|&lane| self.slots[lane].used)
                .expect("cap > 0");
            let slot = &mut self.slots[lane];
            self.by_client.remove(&slot.client);
            slot.client = client.to_string();
            slot.window.clear();
            slot.used = self.clock;
            lane
        };
        self.by_client.insert(client.to_string(), lane);
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
        let a = lanes.touch("1.1.1.1");
        lanes.push(a, 7);
        let b = lanes.touch("2.2.2.2");
        assert_ne!(a, b);
        assert_eq!(lanes.touch("1.1.1.1"), a);
        assert_eq!(lanes.window(a).iter().copied().collect::<Vec<_>>(), [7]);
        assert!(lanes.window(b).is_empty());
    }

    #[test]
    fn the_window_keeps_the_last_eight() {
        let mut lanes = Lanes::new(1);
        let a = lanes.touch("c");
        for token in 0..10 {
            lanes.push(a, token);
        }
        assert_eq!(
            lanes.window(a).iter().copied().collect::<Vec<_>>(),
            (2..10).collect::<Vec<u16>>()
        );
    }

    #[test]
    fn a_full_table_evicts_the_client_idle_longest() {
        let mut lanes = Lanes::new(2);
        let a = lanes.touch("a");
        lanes.push(a, 1);
        let b = lanes.touch("b");
        lanes.touch("a"); // a is now the more recent of the two
        let c = lanes.touch("c");
        assert_eq!(c, b, "c takes b's slot");
        assert!(lanes.window(c).is_empty(), "b's window went with it");
        assert_eq!(lanes.len(), 2);
        // b is gone; touching it again opens a fresh lane by evicting a.
        let b2 = lanes.touch("b");
        assert_eq!(b2, a);
        assert!(lanes.window(b2).is_empty());
    }
}
