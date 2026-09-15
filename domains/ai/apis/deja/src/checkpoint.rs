//! The learned state on disk: one JSON file, written whole to a sibling
//! and renamed over the old one, so a crash mid-write leaves the last
//! good checkpoint in place.

use std::{fs, io, path::Path};

use crate::engine::Snapshot;

pub fn save(path: &Path, snapshot: &Snapshot) -> io::Result<()> {
    let json = serde_json::to_vec(snapshot)?;
    let tmp = path.with_extension("json.tmp");
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    fs::write(&tmp, json)?;
    fs::rename(&tmp, path)
}

/// The checkpoint at `path`, or none when there is no file there. A file
/// that will not parse is an error: starting fresh over a checkpoint that
/// exists is the operator's call, not a silent default.
pub fn load(path: &Path) -> io::Result<Option<Snapshot>> {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error),
    };
    serde_json::from_slice(&bytes)
        .map(Some)
        .map_err(io::Error::other)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::engine::{Engine, fixtures::*};

    #[test]
    fn a_checkpoint_round_trips_and_a_missing_one_is_none() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("state").join("checkpoint.json");
        assert_eq!(load(&path).unwrap(), None);
        let mut engine = Engine::default();
        engine.ingest(&request(
            1.0,
            "1.1.1.1",
            "GET",
            "/iili/v1/r/x",
            302,
            BROWSER,
        ));
        save(&path, &engine.snapshot()).unwrap();
        assert_eq!(load(&path).unwrap(), Some(engine.snapshot()));
        assert!(
            !dir.path()
                .join("state")
                .join("checkpoint.json.tmp")
                .exists()
        );
    }

    #[test]
    fn a_corrupt_checkpoint_is_an_error_not_a_fresh_start() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("checkpoint.json");
        fs::write(&path, b"{not json").unwrap();
        assert!(load(&path).is_err());
    }
}
