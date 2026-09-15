//! Follows Caddy's live access log a line at a time, across the rolls
//! Caddy makes by size: a roll renames the file away and starts a new one,
//! so the reader watches the inode and reopens from the top when it
//! changes, or when the file it holds shrinks under it.

use std::{
    fs::File,
    io::{self, Read, Seek, SeekFrom},
    os::unix::fs::MetadataExt,
    path::{Path, PathBuf},
};

pub struct Tailer {
    path: PathBuf,
    file: Option<Open>,
    partial: Vec<u8>,
}

struct Open {
    file: File,
    ino: u64,
    offset: u64,
}

/// Where a fresh tail starts in the file it finds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Start {
    /// Every line already there: what a cold start learns from.
    Beginning,
    /// Only what is written from now on: what a restart with a checkpoint
    /// wants, having learned the rest already.
    End,
}

impl Tailer {
    pub fn new(path: impl Into<PathBuf>, start: Start) -> io::Result<Self> {
        let path = path.into();
        let file = match File::open(&path) {
            Ok(file) => Some(Open::at(file, start)?),
            // Not there yet: the next poll keeps trying, from the top.
            Err(error) if error.kind() == io::ErrorKind::NotFound => None,
            Err(error) => return Err(error),
        };
        Ok(Self {
            path,
            file,
            partial: Vec::new(),
        })
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    /// The complete lines written since the last poll, newline stripped.
    /// A line still being written waits for its newline; a file that was
    /// rolled or truncated is reopened from the top, and one that is
    /// missing yields nothing until it appears.
    pub fn poll(&mut self) -> io::Result<Vec<Vec<u8>>> {
        let current = match std::fs::metadata(&self.path) {
            Ok(meta) => meta,
            Err(error) if error.kind() == io::ErrorKind::NotFound => {
                self.file = None;
                return Ok(Vec::new());
            }
            Err(error) => return Err(error),
        };
        let stale = match &self.file {
            Some(open) => open.ino != current.ino() || current.len() < open.offset,
            None => true,
        };
        if stale {
            self.file = Some(Open::at(File::open(&self.path)?, Start::Beginning)?);
            self.partial.clear();
        }
        let open = self.file.as_mut().expect("opened above");
        let mut chunk = Vec::new();
        open.file.read_to_end(&mut chunk)?;
        open.offset += chunk.len() as u64;
        self.partial.extend_from_slice(&chunk);

        let mut lines = Vec::new();
        while let Some(newline) = self.partial.iter().position(|&b| b == b'\n') {
            let mut line: Vec<u8> = self.partial.drain(..=newline).collect();
            line.pop();
            lines.push(line);
        }
        Ok(lines)
    }
}

impl Open {
    fn at(mut file: File, start: Start) -> io::Result<Self> {
        let meta = file.metadata()?;
        let offset = match start {
            Start::Beginning => 0,
            Start::End => file.seek(SeekFrom::End(0))?,
        };
        Ok(Self {
            file,
            ino: meta.ino(),
            offset,
        })
    }
}

#[cfg(test)]
mod tests {
    use std::{fs, io::Write};

    use super::*;

    fn lines(tailer: &mut Tailer) -> Vec<String> {
        tailer
            .poll()
            .unwrap()
            .into_iter()
            .map(|line| String::from_utf8(line).unwrap())
            .collect()
    }

    fn append(path: &Path, text: &str) {
        let mut file = fs::OpenOptions::new()
            .append(true)
            .create(true)
            .open(path)
            .unwrap();
        file.write_all(text.as_bytes()).unwrap();
    }

    #[test]
    fn a_cold_start_reads_from_the_top_and_a_restart_from_the_end() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("access.log");
        append(&path, "one\ntwo\n");
        let mut cold = Tailer::new(&path, Start::Beginning).unwrap();
        assert_eq!(lines(&mut cold), ["one", "two"]);
        let mut warm = Tailer::new(&path, Start::End).unwrap();
        assert!(lines(&mut warm).is_empty());
        append(&path, "three\n");
        assert_eq!(lines(&mut warm), ["three"]);
        assert_eq!(lines(&mut cold), ["three"]);
    }

    #[test]
    fn a_line_waits_for_its_newline() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("access.log");
        append(&path, "par");
        let mut tailer = Tailer::new(&path, Start::Beginning).unwrap();
        assert!(lines(&mut tailer).is_empty());
        append(&path, "tial\nnext\n");
        assert_eq!(lines(&mut tailer), ["partial", "next"]);
        assert!(lines(&mut tailer).is_empty());
    }

    // Caddy's roller renames the live file and creates a new one at the
    // same path; the lines it wrote to the old one before the rename are
    // read, and the new file is read from its start.
    #[test]
    fn a_roll_is_followed_to_the_new_file() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("access.log");
        append(&path, "old-1\n");
        let mut tailer = Tailer::new(&path, Start::End).unwrap();
        append(&path, "old-2\n");
        fs::rename(&path, dir.path().join("access-2026-09-15-size.log")).unwrap();
        append(&path, "new-1\n");
        assert_eq!(lines(&mut tailer), ["new-1"]);
        append(&path, "new-2\n");
        assert_eq!(lines(&mut tailer), ["new-2"]);
    }

    // A file shorter than where the reader was is a new file with the same
    // inode, which a truncate-and-reuse roller produces.
    #[test]
    fn a_truncation_is_read_from_the_top() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("access.log");
        append(&path, "aaaa\nbbbb\n");
        let mut tailer = Tailer::new(&path, Start::Beginning).unwrap();
        assert_eq!(lines(&mut tailer), ["aaaa", "bbbb"]);
        fs::write(&path, "c\n").unwrap();
        assert_eq!(lines(&mut tailer), ["c"]);
    }

    #[test]
    fn a_missing_file_yields_nothing_until_it_appears() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("access.log");
        let mut tailer = Tailer::new(&path, Start::End).unwrap();
        assert!(lines(&mut tailer).is_empty());
        append(&path, "first\n");
        assert_eq!(lines(&mut tailer), ["first"]);
        fs::remove_file(&path).unwrap();
        assert!(lines(&mut tailer).is_empty());
        append(&path, "again\n");
        assert_eq!(lines(&mut tailer), ["again"]);
    }
}
