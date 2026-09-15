//! The three ways out: the live stream, the ring, and the counters.

use std::{
    convert::Infallible,
    sync::{
        Arc, Mutex,
        atomic::{AtomicUsize, Ordering},
    },
};

use axum::{
    Json,
    extract::{Query, State},
    http::StatusCode,
    response::{
        IntoResponse, Response,
        sse::{Event as SseEvent, KeepAlive, Sse},
    },
};
use serde::{Deserialize, Serialize};
use tokio::sync::broadcast;
use tokio_stream::{Stream, StreamExt, wrappers::BroadcastStream};

use crate::{
    engine::{Engine, Event},
    metrics::AppMetrics,
};

/// Concurrent stream subscribers; the page is the one consumer, the hub
/// polls `recent`, and the rest is headroom against a page left open in
/// many tabs.
pub const MAX_SUBSCRIBERS: usize = 32;
/// Events a slow subscriber may fall behind before it is dropped to
/// reconnect through `recent?after=`.
const STREAM_BACKLOG: usize = 64;

pub struct AppState {
    pub engine: Mutex<Engine>,
    pub events: broadcast::Sender<Arc<Event>>,
    pub subscribers: AtomicUsize,
    pub metrics: AppMetrics,
}

impl AppState {
    pub fn new(engine: Engine, metrics: AppMetrics) -> Arc<Self> {
        let (events, _) = broadcast::channel(STREAM_BACKLOG);
        Arc::new(Self {
            engine: Mutex::new(engine),
            events,
            subscribers: AtomicUsize::new(0),
            metrics,
        })
    }

    /// Scores one line, records it, and fans it out to the stream.
    pub fn ingest(&self, line: &caddylog::CaddyLine) -> Arc<Event> {
        let event = self.engine.lock().expect("engine lock").ingest(line);
        self.metrics.record(&event);
        // No subscriber is not an error.
        let _ = self.events.send(Arc::clone(&event));
        event
    }
}

pub async fn get_state(State(state): State<Arc<AppState>>) -> Response {
    let view = state.engine.lock().expect("engine lock").state();
    Json(view).into_response()
}

#[derive(Deserialize)]
pub struct After {
    #[serde(default)]
    pub after: u64,
}

#[derive(Serialize)]
struct Recent {
    events: Vec<Arc<Event>>,
}

pub async fn get_recent(
    State(state): State<Arc<AppState>>,
    Query(query): Query<After>,
) -> Response {
    let events = state
        .engine
        .lock()
        .expect("engine lock")
        .recent(query.after);
    Json(Recent { events }).into_response()
}

/// Server-sent events, one per request, `id` the sequence number so a
/// reconnecting page resumes through `recent?after=` from where it was.
pub async fn get_stream(State(state): State<Arc<AppState>>) -> Response {
    let Some(seat) = Seat::take(&state) else {
        return (StatusCode::SERVICE_UNAVAILABLE, "stream is full").into_response();
    };
    let stream = event_stream(state.events.subscribe(), seat);
    Sse::new(stream)
        .keep_alive(KeepAlive::default())
        .into_response()
}

fn event_stream(
    receiver: broadcast::Receiver<Arc<Event>>,
    seat: Seat,
) -> impl Stream<Item = Result<SseEvent, Infallible>> {
    BroadcastStream::new(receiver).filter_map(move |item| {
        // The seat is released when the stream is dropped with the client.
        let _held = &seat;
        // A lagged receiver missed events; the client's next reconnect
        // fills the gap from `recent`, so the lag itself is not an event.
        let event = item.ok()?;
        let data = serde_json::to_string(&*event).ok()?;
        Some(Ok(SseEvent::default().id(event.seq.to_string()).data(data)))
    })
}

/// One of the `MAX_SUBSCRIBERS` seats, given back on drop.
struct Seat(Arc<AppState>);

impl Seat {
    fn take(state: &Arc<AppState>) -> Option<Self> {
        let taken = state
            .subscribers
            .fetch_update(Ordering::SeqCst, Ordering::SeqCst, |n| {
                (n < MAX_SUBSCRIBERS).then_some(n + 1)
            })
            .is_ok();
        taken.then(|| Seat(Arc::clone(state)))
    }
}

impl Drop for Seat {
    fn drop(&mut self) {
        self.0.subscribers.fetch_sub(1, Ordering::SeqCst);
    }
}

#[cfg(test)]
mod tests {
    use axum::{
        Router,
        body::Body,
        http::{Request, header},
        routing::get,
    };
    use http_body_util::BodyExt;
    use tower::ServiceExt;

    use super::*;
    use crate::engine::fixtures::*;

    fn app(state: Arc<AppState>) -> Router {
        Router::new()
            .route("/deja/v1/state", get(get_state))
            .route("/deja/v1/recent", get(get_recent))
            .route("/deja/v1/stream", get(get_stream))
            .with_state(state)
    }

    fn fresh() -> Arc<AppState> {
        AppState::new(Engine::default(), AppMetrics::new())
    }

    async fn body_text(response: Response) -> String {
        let bytes = response.into_body().collect().await.unwrap().to_bytes();
        String::from_utf8(bytes.to_vec()).unwrap()
    }

    async fn fetch(app: Router, uri: &str) -> Response {
        app.oneshot(Request::builder().uri(uri).body(Body::empty()).unwrap())
            .await
            .unwrap()
    }

    // The consumer boundary games_hub polls from C++ (#1554): the raw
    // bytes, key names and order included. A rename on this side fails
    // here before it fails the lobby.
    #[tokio::test]
    async fn recent_is_pinned_on_the_wire() {
        let state = fresh();
        state.ingest(&request(
            1789500000.25,
            "1.1.1.1",
            "GET",
            "/iili/v1/r/abc",
            302,
            BROWSER,
        ));
        state.ingest(&request(
            1789500001.5,
            "1.1.1.1",
            "GET",
            "/iili/v1/r/abc",
            302,
            BROWSER,
        ));
        let response = fetch(app(state), "/deja/v1/recent?after=1").await;
        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(response.headers()[header::CONTENT_TYPE], "application/json");
        assert_eq!(
            body_text(response).await,
            r#"{"events":[{"seq":2,"ts":1789500001.5,"lane":0,"step":1,"context":["api.muchq.com GET /iili/v1/r/* 302 browser"],"actual":"api.muchq.com GET /iili/v1/r/* 302 browser","predictions":{"bigram":[],"net":null},"surprise":{"bigram":1.0986122886681098,"net":null},"threshold":{"mean":1.0986122886681098,"sigma":0.0},"verdict":"warmup","novel":false,"ewma_loss":{"bigram":1.0986122886681098,"net":null},"vocab_size":3}]}"#
        );
    }

    #[tokio::test]
    async fn recent_defaults_to_everything_and_state_counts() {
        let state = fresh();
        state.ingest(&request(
            1.0,
            "1.1.1.1",
            "GET",
            "/iili/v1/r/abc",
            302,
            BROWSER,
        ));
        state.ingest(&request(
            2.0,
            "1.1.1.1",
            "GET",
            "/iili/v1/r/abc",
            302,
            BROWSER,
        ));
        let all: serde_json::Value = serde_json::from_str(
            &body_text(fetch(app(Arc::clone(&state)), "/deja/v1/recent").await).await,
        )
        .unwrap();
        assert_eq!(all["events"].as_array().unwrap().len(), 2);
        let none: serde_json::Value = serde_json::from_str(
            &body_text(fetch(app(Arc::clone(&state)), "/deja/v1/recent?after=2").await).await,
        )
        .unwrap();
        assert_eq!(none["events"].as_array().unwrap().len(), 0);
        let view = body_text(fetch(app(state), "/deja/v1/state").await).await;
        assert_eq!(
            view,
            r#"{"seq":2,"step":1,"vocab_size":3,"warmup":{"steps":1,"needed":1000},"ewma_loss":{"bigram":1.0986122886681098,"net":null},"threshold":{"mean":1.0986122886681098,"sigma":0.0},"anomalies":0,"novelties":1}"#
        );
    }

    #[tokio::test]
    async fn the_stream_carries_each_event_with_its_sequence_as_the_id() {
        let state = fresh();
        let response = fetch(app(Arc::clone(&state)), "/deja/v1/stream").await;
        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(
            response.headers()[header::CONTENT_TYPE],
            "text/event-stream"
        );
        assert_eq!(state.subscribers.load(Ordering::SeqCst), 1);
        state.ingest(&request(
            1.0,
            "1.1.1.1",
            "GET",
            "/iili/v1/r/abc",
            302,
            BROWSER,
        ));
        let mut body = response.into_body().into_data_stream();
        let frame = String::from_utf8(body.next().await.unwrap().unwrap().to_vec()).unwrap();
        assert!(frame.starts_with("id: 1\ndata: {\"seq\":1,"), "{frame}");
        assert!(frame.ends_with("\n\n"), "{frame}");
        drop(body);
        assert_eq!(
            state.subscribers.load(Ordering::SeqCst),
            0,
            "the seat is given back"
        );
    }

    #[tokio::test]
    async fn the_stream_refuses_a_subscriber_past_the_cap() {
        let state = fresh();
        let mut held = Vec::new();
        for _ in 0..MAX_SUBSCRIBERS {
            let response = fetch(app(Arc::clone(&state)), "/deja/v1/stream").await;
            assert_eq!(response.status(), StatusCode::OK);
            held.push(response);
        }
        let response = fetch(app(Arc::clone(&state)), "/deja/v1/stream").await;
        assert_eq!(response.status(), StatusCode::SERVICE_UNAVAILABLE);
        held.pop();
        let response = fetch(app(state), "/deja/v1/stream").await;
        assert_eq!(
            response.status(),
            StatusCode::OK,
            "a dropped client frees its seat"
        );
    }
}
