use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use uuid::Uuid;

use super::card::{EvidenceCard, EvidenceSource};
use crate::archetype::Archetype;

/// Local file-backed store for evidence cards.
///
/// Cards are persisted as individual JSON files under
/// `<data_dir>/evidence/`. The store provides query methods
/// for filtering by source, rubric dimension, and archetype.
///
/// The store tracks the evidence directory's modification time
/// so that `refresh()` can skip re-reading when nothing has changed.
pub struct EvidenceStore {
    dir: PathBuf,
    cards: HashMap<Uuid, EvidenceCard>,
    /// Last-seen modification time of the evidence directory.
    dir_mtime: Option<SystemTime>,
}

impl EvidenceStore {
    /// Open (or create) a store rooted at `dir`.
    pub fn open(dir: &Path) -> Result<Self, StoreError> {
        let evidence_dir = dir.join("evidence");
        fs::create_dir_all(&evidence_dir).map_err(StoreError::Io)?;

        let mut cards = HashMap::new();
        for entry in fs::read_dir(&evidence_dir).map_err(StoreError::Io)? {
            let entry = entry.map_err(StoreError::Io)?;
            let path = entry.path();
            if path.extension().is_some_and(|ext| ext == "json") {
                let data = fs::read_to_string(&path).map_err(StoreError::Io)?;
                let card: EvidenceCard =
                    serde_json::from_str(&data).map_err(StoreError::Parse)?;
                cards.insert(card.id, card);
            }
        }

        let dir_mtime = fs::metadata(&evidence_dir)
            .and_then(|m| m.modified())
            .ok();

        Ok(Self {
            dir: evidence_dir,
            cards,
            dir_mtime,
        })
    }

    /// Persist a new card to disk and add it to the in-memory index.
    pub fn insert(&mut self, card: EvidenceCard) -> Result<(), StoreError> {
        let path = self.card_path(card.id);
        let data = serde_json::to_string_pretty(&card).map_err(StoreError::Parse)?;
        fs::write(&path, data).map_err(StoreError::Io)?;
        self.cards.insert(card.id, card);
        self.sync_mtime();
        Ok(())
    }

    /// Remove a card by id.
    pub fn remove(&mut self, id: Uuid) -> Result<Option<EvidenceCard>, StoreError> {
        let path = self.card_path(id);
        if path.exists() {
            fs::remove_file(&path).map_err(StoreError::Io)?;
        }
        let removed = self.cards.remove(&id);
        self.sync_mtime();
        Ok(removed)
    }

    /// Re-read cards from disk if the directory has been modified since
    /// our last load. Uses the directory mtime as a cheap change indicator
    /// so callers can invoke this on every read without I/O overhead when
    /// nothing has changed.
    pub fn refresh(&mut self) -> Result<(), StoreError> {
        let current_mtime = fs::metadata(&self.dir)
            .and_then(|m| m.modified())
            .ok();

        if current_mtime == self.dir_mtime {
            return Ok(());
        }

        let mut cards = HashMap::new();
        for entry in fs::read_dir(&self.dir).map_err(StoreError::Io)? {
            let entry = entry.map_err(StoreError::Io)?;
            let path = entry.path();
            if path.extension().is_some_and(|ext| ext == "json") {
                let data = fs::read_to_string(&path).map_err(StoreError::Io)?;
                let card: EvidenceCard =
                    serde_json::from_str(&data).map_err(StoreError::Parse)?;
                cards.insert(card.id, card);
            }
        }
        self.cards = cards;
        self.dir_mtime = current_mtime;
        Ok(())
    }

    pub fn get(&self, id: Uuid) -> Option<&EvidenceCard> {
        self.cards.get(&id)
    }

    pub fn all(&self) -> Vec<&EvidenceCard> {
        self.cards.values().collect()
    }

    pub fn count(&self) -> usize {
        self.cards.len()
    }

    /// Cards originating from a given source.
    pub fn by_source(&self, source: EvidenceSource) -> Vec<&EvidenceCard> {
        self.cards
            .values()
            .filter(|c| c.source == source)
            .collect()
    }

    /// Cards tagged with a given rubric dimension.
    pub fn by_rubric_tag(&self, tag: &str) -> Vec<&EvidenceCard> {
        self.cards
            .values()
            .filter(|c| c.rubric_tags.iter().any(|t| t == tag))
            .collect()
    }

    /// Cards tagged with a given archetype.
    pub fn by_archetype(&self, archetype: Archetype) -> Vec<&EvidenceCard> {
        self.cards
            .values()
            .filter(|c| c.archetype_tags.contains(&archetype))
            .collect()
    }

    /// Update stored mtime to match the directory's current state,
    /// so a subsequent `refresh()` won't re-read our own writes.
    fn sync_mtime(&mut self) {
        self.dir_mtime = fs::metadata(&self.dir)
            .and_then(|m| m.modified())
            .ok();
    }

    fn card_path(&self, id: Uuid) -> PathBuf {
        self.dir.join(format!("{id}.json"))
    }
}

#[derive(Debug)]
pub enum StoreError {
    Io(std::io::Error),
    Parse(serde_json::Error),
}

impl std::fmt::Display for StoreError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "store I/O error: {e}"),
            Self::Parse(e) => write!(f, "store parse error: {e}"),
        }
    }
}

impl std::error::Error for StoreError {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;
    use std::time::Duration;
    use tempfile::TempDir;

    fn tmp_store() -> (TempDir, EvidenceStore) {
        let dir = TempDir::new().unwrap();
        let store = EvidenceStore::open(dir.path()).unwrap();
        (dir, store)
    }

    #[test]
    fn refresh_picks_up_external_writes() {
        let (dir, mut store_a) = tmp_store();

        // Store A inserts a card
        let card = EvidenceCard::new(EvidenceSource::Manual, "card from A");
        store_a.insert(card).unwrap();
        assert_eq!(store_a.count(), 1);

        // Store B opens the same directory — simulates another process
        let mut store_b = EvidenceStore::open(dir.path()).unwrap();
        assert_eq!(store_b.count(), 1);

        // Store A adds another card
        let card2 = EvidenceCard::new(EvidenceSource::Github, "card from A again");
        store_a.insert(card2).unwrap();
        assert_eq!(store_a.count(), 2);

        // Store B doesn't see it yet (stale in-memory cache)
        assert_eq!(store_b.count(), 1);

        // After refresh, store B picks up the new card
        store_b.refresh().unwrap();
        assert_eq!(store_b.count(), 2);
    }

    #[test]
    fn refresh_picks_up_external_deletes() {
        let (dir, mut store_a) = tmp_store();

        let card = EvidenceCard::new(EvidenceSource::Manual, "will be deleted");
        let id = card.id;
        store_a.insert(card).unwrap();

        let mut store_b = EvidenceStore::open(dir.path()).unwrap();
        assert_eq!(store_b.count(), 1);

        // Store A removes the card
        store_a.remove(id).unwrap();
        assert_eq!(store_a.count(), 0);

        // Store B still sees it until refresh
        assert_eq!(store_b.count(), 1);
        store_b.refresh().unwrap();
        assert_eq!(store_b.count(), 0);
    }

    #[test]
    fn refresh_skips_reread_when_unchanged() {
        let (_dir, mut store) = tmp_store();

        let card = EvidenceCard::new(EvidenceSource::Manual, "test");
        store.insert(card).unwrap();

        // Capture mtime after insert
        let mtime_after_insert = store.dir_mtime;

        // refresh should be a no-op (mtime unchanged)
        store.refresh().unwrap();
        assert_eq!(store.dir_mtime, mtime_after_insert);
        assert_eq!(store.count(), 1);
    }

    #[test]
    fn refresh_detects_mtime_change_from_external_write() {
        let (dir, mut store) = tmp_store();

        let mtime_before = store.dir_mtime;

        // Simulate external process writing a card directly to disk
        // Sleep briefly so mtime differs (filesystem granularity)
        thread::sleep(Duration::from_millis(50));
        let card = EvidenceCard::new(EvidenceSource::Slack, "external card");
        let path = dir.path().join("evidence").join(format!("{}.json", card.id));
        let data = serde_json::to_string_pretty(&card).unwrap();
        fs::write(&path, data).unwrap();

        // Store doesn't see it yet
        assert_eq!(store.count(), 0);

        // Mtime should have changed on disk
        let current_mtime = fs::metadata(dir.path().join("evidence"))
            .and_then(|m| m.modified())
            .ok();
        assert_ne!(current_mtime, mtime_before);

        // refresh detects the change and reloads
        store.refresh().unwrap();
        assert_eq!(store.count(), 1);
        assert_eq!(store.all()[0].summary, "external card");
    }

    #[test]
    fn insert_syncs_mtime_so_refresh_is_noop() {
        let (_dir, mut store) = tmp_store();

        let card = EvidenceCard::new(EvidenceSource::Manual, "my card");
        store.insert(card).unwrap();

        let mtime_after = store.dir_mtime;

        // refresh should not change anything
        store.refresh().unwrap();
        assert_eq!(store.dir_mtime, mtime_after);
        assert_eq!(store.count(), 1);
    }
}
