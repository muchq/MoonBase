//! The learned state on disk: one JSON file, written whole to a sibling
//! and renamed over the old one, so a crash mid-write leaves the last
//! good checkpoint in place.

use std::{
    fs, io,
    path::{Path, PathBuf},
    sync::atomic::{AtomicU64, Ordering},
};

use crate::engine::Snapshot;

/// Distinguishes the temporaries of two writers racing for the same path,
/// so neither renames the other's half-written file into place.
static WRITES: AtomicU64 = AtomicU64::new(0);

pub fn save(path: &Path, snapshot: &Snapshot) -> io::Result<()> {
    let json = serde_json::to_vec(snapshot)?;
    let tmp = temporary(path);
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    fs::write(&tmp, json)?;
    fs::rename(&tmp, path)
}

/// The checkpoint at `path`, or none when there is no file there. A file
/// that will not parse is moved aside to `<path>.corrupt` and reported
/// as none: the service starts fresh rather than staying down, and the
/// evidence stays on disk.
pub fn load_or_quarantine(path: &Path) -> io::Result<Option<Snapshot>> {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error),
    };
    match serde_json::from_slice(&bytes) {
        Ok(snapshot) => Ok(Some(snapshot)),
        Err(error) => {
            let aside = quarantine_path(path);
            tracing::error!(%error, path = %path.display(), aside = %aside.display(), "checkpoint unreadable; moved aside");
            fs::rename(path, aside)?;
            Ok(None)
        }
    }
}

/// A sibling of `path` no other call has handed out.
fn temporary(path: &Path) -> PathBuf {
    let write = WRITES.fetch_add(1, Ordering::Relaxed);
    path.with_extension(format!("json.tmp.{}.{write}", std::process::id()))
}

fn quarantine_path(path: &Path) -> PathBuf {
    let mut name = path.as_os_str().to_owned();
    name.push(".corrupt");
    PathBuf::from(name)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::engine::{Engine, fixtures::*};

    #[test]
    fn a_checkpoint_round_trips_and_a_missing_one_is_none() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("state").join("checkpoint.json");
        assert_eq!(load_or_quarantine(&path).unwrap(), None);
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
        assert_eq!(load_or_quarantine(&path).unwrap(), Some(engine.snapshot()));
        let left: Vec<_> = fs::read_dir(path.parent().unwrap())
            .unwrap()
            .map(|entry| entry.unwrap().file_name())
            .collect();
        assert_eq!(left, ["checkpoint.json"], "no temporary is left behind");
    }

    #[test]
    fn a_corrupt_checkpoint_is_moved_aside_and_the_start_is_fresh() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("checkpoint.json");
        fs::write(&path, b"{not json").unwrap();
        assert_eq!(load_or_quarantine(&path).unwrap(), None);
        assert!(!path.exists());
        assert_eq!(
            fs::read(dir.path().join("checkpoint.json.corrupt")).unwrap(),
            b"{not json"
        );
        assert_eq!(load_or_quarantine(&path).unwrap(), None);
    }

    // Two saves in flight write to two temporaries: a shared one could be
    // renamed into place by the first writer while the second is still
    // filling it.
    #[test]
    fn concurrent_saves_do_not_share_a_temporary() {
        let path = Path::new("/var/lib/deja/checkpoint.json");
        let (a, b) = (temporary(path), temporary(path));
        assert_ne!(a, b);
        assert_eq!(a.parent(), path.parent());
        assert!(
            a.file_name()
                .unwrap()
                .to_str()
                .unwrap()
                .starts_with("checkpoint.json.tmp")
        );
    }
}
