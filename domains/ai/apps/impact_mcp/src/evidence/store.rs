use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use uuid::Uuid;

use super::card::{EvidenceCard, EvidenceSource};
use crate::archetype::Archetype;

/// Local file-backed store for evidence cards.
///
/// Cards are persisted as individual JSON files under
/// `<data_dir>/evidence/`. The store provides query methods
/// for filtering by source, rubric dimension, and archetype.
pub struct EvidenceStore {
    dir: PathBuf,
    cards: HashMap<Uuid, EvidenceCard>,
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

        Ok(Self {
            dir: evidence_dir,
            cards,
        })
    }

    /// Persist a new card to disk and add it to the in-memory index.
    pub fn insert(&mut self, card: EvidenceCard) -> Result<(), StoreError> {
        let path = self.card_path(card.id);
        let data = serde_json::to_string_pretty(&card).map_err(StoreError::Parse)?;
        fs::write(&path, data).map_err(StoreError::Io)?;
        self.cards.insert(card.id, card);
        Ok(())
    }

    /// Remove a card by id.
    pub fn remove(&mut self, id: Uuid) -> Result<Option<EvidenceCard>, StoreError> {
        let path = self.card_path(id);
        if path.exists() {
            fs::remove_file(&path).map_err(StoreError::Io)?;
        }
        Ok(self.cards.remove(&id))
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
