//! Follows Caddy's live access log a line at a time, across the rolls
//! Caddy makes by size: a roll renames the file away and starts a new one,
//! so the reader watches the inode, drains what it still holds, and
//! reopens from the top when the inode changes or the file shrinks under
//! it.

use std::{
    fs::File,
    io::{self, Read, Seek, SeekFrom},
    os::unix::fs::MetadataExt,
    path::{Path, PathBuf},
};

/// Bytes a line may run to before it is dropped rather than buffered;
/// Caddy's lines are a few hundred bytes, and a file that is not a log at
/// all must not grow `partial` without bound.
pub const MAX_LINE: usize = 1024 * 1024;

pub struct Tailer {
    path: PathBuf,
    file: Option<Open>,
    partial: Vec<u8>,
    /// The current line already overran `MAX_LINE`; the rest of it is
    /// dropped up to its newline.
    skipping: bool,
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
            skipping: false,
        })
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    /// The complete lines written since the last poll, newline stripped.
    /// A line still being written waits for its newline; a file that was
    /// rolled away is drained and then the new one is read from the top; a
    /// truncated file is read from the top; a missing one yields nothing
    /// until it appears.
    pub fn poll(&mut self) -> io::Result<Vec<Vec<u8>>> {
        let mut lines = Vec::new();
        let current = match std::fs::metadata(&self.path) {
            Ok(meta) => meta,
            Err(error) if error.kind() == io::ErrorKind::NotFound => {
                self.file = None;
                return Ok(lines);
            }
            Err(error) => return Err(error),
        };
        let stale = match &self.file {
            Some(open) => open.ino != current.ino() || current.len() < open.offset,
            None => true,
        };
        if stale {
            // A rolled file was renamed, not truncated: whatever Caddy wrote
            // to it between the last poll and the rename is still there.
            if let Some(open) = self.file.as_mut().filter(|open| open.ino != current.ino()) {
                let chunk = open.read_new()?;
                self.split(&chunk, &mut lines);
            }
            self.file = Some(Open::at(File::open(&self.path)?, Start::Beginning)?);
            self.partial.clear();
            self.skipping = false;
        }
        let chunk = self.file.as_mut().expect("opened above").read_new()?;
        self.split(&chunk, &mut lines);
        Ok(lines)
    }

    /// Appends the complete lines in `chunk` (with what `partial` held) to
    /// `lines`, keeping the trailing incomplete one, and dropping any line
    /// past `MAX_LINE` bytes.
    fn split(&mut self, chunk: &[u8], lines: &mut Vec<Vec<u8>>) {
        let mut rest = chunk;
        while let Some(newline) = rest.iter().position(|&b| b == b'\n') {
            let (head, tail) = rest.split_at(newline);
            rest = &tail[1..];
            if self.skipping {
                self.skipping = false;
                continue;
            }
            if self.partial.len() + head.len() > MAX_LINE {
                self.partial.clear();
                continue;
            }
            let mut line = std::mem::take(&mut self.partial);
            line.extend_from_slice(head);
            lines.push(line);
        }
        if self.skipping {
            return;
        }
        if self.partial.len() + rest.len() > MAX_LINE {
            self.partial.clear();
            self.skipping = true;
        } else {
            self.partial.extend_from_slice(rest);
        }
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

    /// Everything written since the last read.
    fn read_new(&mut self) -> io::Result<Vec<u8>> {
        let mut chunk = Vec::new();
        self.file.read_to_end(&mut chunk)?;
        self.offset += chunk.len() as u64;
        Ok(chunk)
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
    // same path; the lines it wrote to the old one between the last poll
    // and the rename are read first, then the new file from its start.
    #[test]
    fn a_roll_is_drained_and_followed_to_the_new_file() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("access.log");
        append(&path, "old-1\n");
        let mut tailer = Tailer::new(&path, Start::End).unwrap();
        append(&path, "old-2\n");
        fs::rename(&path, dir.path().join("access-2026-09-15-size.log")).unwrap();
        append(&path, "new-1\n");
        assert_eq!(lines(&mut tailer), ["old-2", "new-1"]);
        append(&path, "new-2\n");
        assert_eq!(lines(&mut tailer), ["new-2"]);
    }

    // A partial line at the moment of the roll belongs to the old file;
    // Caddy finishes it there before it opens the new one.
    #[test]
    fn a_partial_line_is_completed_from_the_rolled_file() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("access.log");
        let mut tailer = Tailer::new(&path, Start::Beginning).unwrap();
        append(&path, "old-");
        assert!(lines(&mut tailer).is_empty());
        append(&path, "1\n");
        fs::rename(&path, dir.path().join("access-rolled.log")).unwrap();
        append(&path, "new-1\n");
        assert_eq!(lines(&mut tailer), ["old-1", "new-1"]);
    }

    // An oversized line is dropped whole, however it arrives, and the
    // lines around it are not.
    #[test]
    fn a_line_past_max_line_is_dropped_and_the_next_one_is_read() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("access.log");
        let mut tailer = Tailer::new(&path, Start::Beginning).unwrap();
        let huge = "x".repeat(MAX_LINE + 1);
        append(&path, &format!("before\n{huge}\nafter\n"));
        assert_eq!(lines(&mut tailer), ["before", "after"]);
        // The same line arriving in pieces across polls.
        append(&path, &huge[..MAX_LINE / 2]);
        assert!(lines(&mut tailer).is_empty());
        append(&path, &huge[MAX_LINE / 2..]);
        assert!(lines(&mut tailer).is_empty());
        append(&path, "\nlast\n");
        assert_eq!(lines(&mut tailer), ["last"]);
        // Exactly MAX_LINE bytes is a line.
        append(&path, &format!("{}\n", &huge[..MAX_LINE]));
        assert_eq!(lines(&mut tailer)[0].len(), MAX_LINE);
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
