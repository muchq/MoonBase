mod api;
mod bigram;
mod checkpoint;
mod engine;
mod lanes;
mod metrics;
mod score;
mod tail;
mod token;

use std::{env, path::PathBuf, process, sync::Arc, thread, time::Duration};

use axum::routing::get;
use caddylog::CaddyLine;
use server_pal::{listen_addr_pal, router_builder, serve};
use tokio::{signal, sync::mpsc};
use tracing::{Level, event};

use crate::{
    api::{AppState, get_recent, get_state, get_stream},
    engine::Engine,
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
    let checkpoint_path = PathBuf::from(env_or("CHECKPOINT", "/var/lib/deja/checkpoint.json"));
    let checkpoint_every = Duration::from_secs(
        env_or("CHECKPOINT_INTERVAL_SECS", "600")
            .parse()
            .unwrap_or_else(|e| {
                eprintln!("error: CHECKPOINT_INTERVAL_SECS: {e}");
                process::exit(1);
            }),
    );

    // A checkpoint means the file's past is learned already; none means
    // it is the warmup.
    let (engine, start) = match checkpoint::load(&checkpoint_path) {
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
    let (lines_tx, mut lines_rx) = mpsc::channel::<Vec<u8>>(1024);
    thread::spawn(move || follow(tailer, lines_tx));
    tokio::spawn({
        let state = Arc::clone(&state);
        async move {
            while let Some(line) = lines_rx.recv().await {
                match CaddyLine::parse(&line) {
                    Ok(line) => {
                        state.ingest(&line);
                    }
                    Err(error) => event!(Level::WARN, %error, "skipping an unreadable line"),
                }
            }
        }
    });
    tokio::spawn({
        let state = Arc::clone(&state);
        let path = checkpoint_path.clone();
        async move {
            let mut ticks = tokio::time::interval(checkpoint_every);
            ticks.tick().await;
            loop {
                ticks.tick().await;
                save(&state, &path);
            }
        }
    });

    let app = router_builder()
        .route("/deja/v1/state", get(get_state))
        .route("/deja/v1/recent", get(get_recent))
        .stream_route("/deja/v1/stream", get(get_stream))
        .build()
        .with_state(Arc::clone(&state));
    let listen_address = listen_addr_pal();
    event!(Level::INFO, "listening on {listen_address}");
    tokio::select! {
        () = serve(app, &listen_address) => {}
        () = terminated() => {
            event!(Level::INFO, "stopping; writing the checkpoint");
            save(&state, &checkpoint_path);
        }
    }
}

/// Reads the log on its own thread, since the tail is plain blocking IO,
/// and hands lines to the engine. A read error is logged and retried on
/// the next poll: the log is Caddy's, and the next roll may fix it.
fn follow(mut tailer: Tailer, lines: mpsc::Sender<Vec<u8>>) {
    loop {
        match tailer.poll() {
            Ok(batch) => {
                for line in batch {
                    if lines.blocking_send(line).is_err() {
                        return;
                    }
                }
            }
            Err(error) => event!(Level::WARN, %error, path = %tailer.path().display(), "tail"),
        }
        thread::sleep(POLL);
    }
}

fn save(state: &AppState, path: &std::path::Path) {
    let snapshot = state.engine.lock().expect("engine lock").snapshot();
    match checkpoint::save(path, &snapshot) {
        Ok(()) => event!(Level::INFO, seq = snapshot.seq, "checkpoint written"),
        Err(error) => {
            event!(Level::ERROR, %error, path = %path.display(), "checkpoint not written")
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
