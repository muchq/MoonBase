mod api;
mod bigram;
mod checkpoint;
mod engine;
mod lanes;
mod metrics;
mod net;
mod score;
mod tail;
mod token;

use std::{
    env,
    path::PathBuf,
    process,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    thread,
    time::Duration,
};

use caddylog::CaddyLine;
use server_pal::{listen_addr_pal, serve};
use tokio::signal;
use tracing::{Level, event};

use crate::{
    api::AppState,
    engine::{Engine, Snapshot},
    metrics::AppMetrics,
    tail::{Start, Tailer},
};

const POLL: Duration = Duration::from_millis(500);

fn env_or(name: &str, default: &str) -> String {
    env::var(name).unwrap_or_else(|_| default.to_string())
}

#[tokio::main]
async fn main() {
    server_pal::init_logging();
    let _otel_provider = server_pal::init_otel();

    let log_path = PathBuf::from(env_or("ACCESS_LOG", "/var/log/caddy/access.log"));
    let checkpoint_path =
        PathBuf::from(env_or("DEJA_STATE", "/var/lib/deja")).join("checkpoint.json");
    let checkpoint_every = match env_or("CHECKPOINT_INTERVAL_SECS", "600").parse::<u64>() {
        Ok(secs) if secs > 0 => Duration::from_secs(secs),
        Ok(_) => {
            eprintln!("error: CHECKPOINT_INTERVAL_SECS must be at least 1");
            process::exit(1);
        }
        Err(error) => {
            eprintln!("error: CHECKPOINT_INTERVAL_SECS: {error}");
            process::exit(1);
        }
    };

    // A checkpoint means the file's past is learned already; none means
    // it is the warmup.
    let (engine, start) = match checkpoint::load_or_quarantine(&checkpoint_path) {
        Ok(Some(snapshot)) => {
            event!(Level::INFO, seq = snapshot.seq, "resuming from checkpoint");
            (
                Engine::from_snapshot(snapshot, token::DEFAULT_CAP, lanes::DEFAULT_LANES),
                Start::End,
            )
        }
        Ok(None) => {
            event!(
                Level::INFO,
                "no checkpoint; learning the current log from the top"
            );
            (Engine::default(), Start::Beginning)
        }
        Err(error) => {
            eprintln!("error: cannot read {}: {error}", checkpoint_path.display());
            process::exit(1);
        }
    };
    let tailer = Tailer::new(&log_path, start).unwrap_or_else(|error| {
        eprintln!("error: cannot open {}: {error}", log_path.display());
        process::exit(1);
    });

    let state = AppState::new(engine, AppMetrics::new());
    let checkpointer = Arc::new(Checkpointer::new(checkpoint_path));
    let stopping = Arc::new(AtomicBool::new(false));
    let reader = thread::spawn({
        let state = Arc::clone(&state);
        let stopping = Arc::clone(&stopping);
        move || follow(tailer, &state, &stopping, POLL)
    });
    tokio::spawn({
        let state = Arc::clone(&state);
        let checkpointer = Arc::clone(&checkpointer);
        async move {
            let mut ticks = tokio::time::interval(checkpoint_every);
            ticks.tick().await;
            loop {
                ticks.tick().await;
                let (state, checkpointer) = (Arc::clone(&state), Arc::clone(&checkpointer));
                // Serializing and writing the file is blocking work.
                let _ = tokio::task::spawn_blocking(move || checkpointer.save(&state)).await;
            }
        }
    });

    let app = api::app(Arc::clone(&state));
    let listen_address = listen_addr_pal();
    event!(Level::INFO, "listening on {listen_address}");
    tokio::select! {
        () = serve(app, &listen_address) => {}
        () = terminated() => {
            // The reader stops first so the checkpoint is of a quiet engine.
            event!(Level::INFO, "stopping; writing the checkpoint");
            stopping.store(true, Ordering::SeqCst);
            let _ = reader.join();
            checkpointer.save(&state);
        }
    }
}

/// Reads the log on its own thread, since the tail is plain blocking IO,
/// and feeds the engine until `stopping` is set. A read error is logged
/// and retried on the next poll: the log is Caddy's, and the next roll may
/// fix it. A line that is not a Caddy line is skipped.
fn follow(mut tailer: Tailer, state: &AppState, stopping: &AtomicBool, poll: Duration) {
    while !stopping.load(Ordering::SeqCst) {
        match tailer.poll() {
            Ok(batch) => {
                for line in batch {
                    match CaddyLine::parse(&line) {
                        Ok(line) => {
                            state.ingest(&line);
                        }
                        Err(error) => event!(Level::WARN, %error, "skipping an unreadable line"),
                    }
                }
            }
            Err(error) => event!(Level::WARN, %error, path = %tailer.path().display(), "tail"),
        }
        thread::sleep(poll);
    }
}

/// Writes checkpoints in sequence order. A snapshot older than one already
/// on disk is dropped, so a periodic save that took its snapshot before the
/// reader stopped cannot land after the final one and rewind the next boot.
struct Checkpointer {
    path: PathBuf,
    written: Mutex<u64>,
}

impl Checkpointer {
    fn new(path: PathBuf) -> Self {
        Self {
            path,
            written: Mutex::new(0),
        }
    }

    fn save(&self, state: &AppState) {
        let snapshot = state.engine.lock().expect("engine lock").snapshot();
        self.write(&snapshot);
    }

    fn write(&self, snapshot: &Snapshot) {
        let mut written = self.written.lock().expect("checkpoint lock");
        if snapshot.seq < *written {
            event!(
                Level::INFO,
                seq = snapshot.seq,
                newer = *written,
                "checkpoint skipped"
            );
            return;
        }
        match checkpoint::save(&self.path, snapshot) {
            Ok(()) => {
                *written = snapshot.seq;
                event!(Level::INFO, seq = snapshot.seq, "checkpoint written");
            }
            Err(error) => {
                event!(Level::ERROR, %error, path = %self.path.display(), "checkpoint not written")
            }
        }
    }
}

async fn terminated() {
    let mut term =
        signal::unix::signal(signal::unix::SignalKind::terminate()).expect("SIGTERM handler");
    tokio::select! {
        _ = signal::ctrl_c() => {}
        _ = term.recv() => {}
    }
}

#[cfg(test)]
mod tests {
    use std::{fs, io::Write};

    use super::*;

    // The reader thread feeds what the tail hands it, skips what does not
    // parse, and returns when told to.
    #[test]
    fn follow_ingests_until_stopped() {
        let dir = tempfile::tempdir().unwrap();
        let log = dir.path().join("access.log");
        fs::write(&log, "not a caddy line\n").unwrap();
        let state = AppState::new(Engine::default(), AppMetrics::new());
        let stopping = Arc::new(AtomicBool::new(false));
        let reader = thread::spawn({
            let state = Arc::clone(&state);
            let stopping = Arc::clone(&stopping);
            let tailer = Tailer::new(&log, Start::Beginning).unwrap();
            move || follow(tailer, &state, &stopping, Duration::from_millis(5))
        });
        let mut file = fs::OpenOptions::new().append(true).open(&log).unwrap();
        writeln!(
            file,
            r#"{{"ts":1.0,"status":200,"request":{{"host":"api.muchq.com","method":"GET","uri":"/","client_ip":"1.1.1.1","headers":{{}}}}}}"#
        )
        .unwrap();
        while state.engine.lock().unwrap().state().seq < 1 {
            thread::sleep(Duration::from_millis(5));
        }
        stopping.store(true, Ordering::SeqCst);
        reader.join().unwrap();
        assert_eq!(state.engine.lock().unwrap().state().seq, 1);
    }

    // The final checkpoint at shutdown is the newest; a periodic save whose
    // snapshot predates it must not replace it however late it writes.
    #[test]
    fn an_older_snapshot_never_overwrites_a_newer_checkpoint() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("checkpoint.json");
        let checkpointer = Checkpointer::new(path.clone());
        let mut engine = Engine::default();
        engine.ingest(&crate::engine::fixtures::request(
            1.0,
            "1.1.1.1",
            "GET",
            "/",
            200,
            crate::engine::fixtures::BROWSER,
        ));
        let older = engine.snapshot();
        engine.ingest(&crate::engine::fixtures::request(
            2.0,
            "1.1.1.1",
            "GET",
            "/",
            200,
            crate::engine::fixtures::BROWSER,
        ));
        let newer = engine.snapshot();
        checkpointer.write(&newer);
        checkpointer.write(&older);
        let on_disk = checkpoint::load_or_quarantine(&path).unwrap().unwrap();
        assert_eq!(on_disk.seq, newer.seq);
        assert_eq!(on_disk, newer);
    }
}
