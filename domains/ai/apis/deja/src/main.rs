mod api;
mod bigram;
mod checkpoint;
mod engine;
mod hub;
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
    // games_hub's domain events (#1572), the second source. Unset, deja
    // reads the access log alone exactly as before — which is also how
    // the bigram control gets its yardstick: the curve with and without.
    let hub_path = env::var("HUB_EVENT_LOG")
        .ok()
        .filter(|path| !path.is_empty())
        .map(PathBuf::from);
    let hub_tailer = hub_path.map(|path| {
        // A directory opens and seeks like a file and then fails every
        // read, which a tail cannot tell from a log that will not settle:
        // it would warn twice a second forever with the source dead and
        // nothing else saying so. The name is one path component away
        // from the directory it lives in, so this is the likely typo.
        if path.is_dir() {
            eprintln!("error: HUB_EVENT_LOG is a directory: {}", path.display());
            process::exit(1);
        }
        Tailer::new(&path, start).unwrap_or_else(|error| {
            eprintln!("error: cannot open {}: {error}", path.display());
            process::exit(1);
        })
    });

    let state = AppState::new(engine, AppMetrics::new());
    let checkpointer = Arc::new(Checkpointer::new(checkpoint_path));
    let stopping = Arc::new(AtomicBool::new(false));
    let reader = thread::spawn({
        let state = Arc::clone(&state);
        let stopping = Arc::clone(&stopping);
        move || follow(tailer, &state, &stopping, POLL)
    });
    // Its own thread, because a tail is blocking IO; they meet at the
    // engine's mutex, which is where one vocabulary and one lane table
    // make the two sources one stream.
    let hub_reader = hub_tailer.map(|tailer| {
        thread::spawn({
            let state = Arc::clone(&state);
            let stopping = Arc::clone(&stopping);
            move || follow_hub(tailer, &state, &stopping, POLL)
        })
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
            if let Some(hub_reader) = hub_reader {
                let _ = hub_reader.join();
            }
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

/// The same for games_hub's event log, whose lines are its own and not
/// Caddy's. A line this build cannot read at all is skipped and logged;
/// an event it does not recognise is not — `hub other` is a token, so a
/// hub that grew an eighth event leaves no hole in the sequence.
fn follow_hub(mut tailer: Tailer, state: &AppState, stopping: &AtomicBool, poll: Duration) {
    while !stopping.load(Ordering::SeqCst) {
        match tailer.poll() {
            Ok(batch) => {
                for line in batch {
                    match hub::observation(&line) {
                        Some(observation) => {
                            state.observe(&observation);
                        }
                        None => event!(Level::WARN, "skipping an unreadable hub event"),
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

    // The one end-to-end claim this source makes: a line the hub wrote
    // reaches the engine as an event keyed on its room. A line that is
    // not a hub event is skipped without stopping the loop, the way an
    // unreadable caddy line is.
    #[test]
    fn follow_hub_feeds_the_engine_from_the_hubs_own_log() {
        let dir = tempfile::tempdir().unwrap();
        let log = dir.path().join("game_events.log");
        fs::write(&log, "{\"level\":\"info\"}\n").unwrap();
        let state = AppState::new(Engine::default(), AppMetrics::new());
        let stopping = Arc::new(AtomicBool::new(false));
        let reader = thread::spawn({
            let state = Arc::clone(&state);
            let stopping = Arc::clone(&stopping);
            let tailer = Tailer::new(&log, Start::Beginning).unwrap();
            move || follow_hub(tailer, &state, &stopping, Duration::from_millis(5))
        });
        let mut file = fs::OpenOptions::new().append(true).open(&log).unwrap();
        writeln!(
            file,
            r#"{{"ts":1750000000123,"event":"room_created","room":"ABC123","surface":"sphere"}}"#
        )
        .unwrap();
        while state.engine.lock().unwrap().state().seq < 1 {
            thread::sleep(Duration::from_millis(5));
        }
        stopping.store(true, Ordering::SeqCst);
        reader.join().unwrap();

        // The junk line was skipped, not counted and not fatal.
        assert_eq!(state.engine.lock().unwrap().state().seq, 1);
        let event = state
            .engine
            .lock()
            .unwrap()
            .recent(0)
            .pop()
            .expect("the hub line reached the ring");
        assert_eq!(event.actual, "hub room_created sphere");
        assert_eq!(event.ts, 1_750_000_000.123);
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
