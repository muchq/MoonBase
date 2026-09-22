//! The ways out: the live stream, the ring, the counters, and a question.

use std::{
    convert::Infallible,
    pin::Pin,
    sync::{
        Arc, Mutex, MutexGuard,
        atomic::{AtomicUsize, Ordering},
    },
    task::{Context, Poll},
    time::Duration,
};

use axum::{
    Json, Router,
    extract::{Query, State, rejection::JsonRejection},
    http::StatusCode,
    response::{
        IntoResponse, Response,
        sse::{Event as SseEvent, KeepAlive, Sse},
    },
    routing::{get, post},
};
use serde::{Deserialize, Serialize};
use tokio::{sync::broadcast, time::Sleep};
use tokio_stream::{Stream, StreamExt, wrappers::BroadcastStream};

use crate::{
    engine::{Engine, Event, Guess, PerPredictor},
    metrics::AppMetrics,
};

pub const STATE_PATH: &str = "/deja/v1/state";
pub const RECENT_PATH: &str = "/deja/v1/recent";
pub const STREAM_PATH: &str = "/deja/v1/stream";
pub const NEXT_PATH: &str = "/deja/v1/next";

/// Concurrent stream subscribers. The page is the one consumer and the hub
/// polls `recent`; the cap is what a page left open in many tabs, or a
/// client that never closes, can hold at once.
pub const MAX_SUBSCRIBERS: usize = 256;
/// A stream ends after this long and the client reconnects, so a seat is
/// never held forever by a connection nobody is reading.
pub const STREAM_LIFETIME: Duration = Duration::from_secs(10 * 60);
/// Events a subscriber may fall behind before its stream ends. Fewer than
/// the ring holds, so the reconnect through `recent?after=` closes the gap.
const STREAM_BACKLOG: usize = 64;

/// Sustained requests per second and burst, per peer address. Everything
/// arriving through Caddy shares one bucket, since Caddy is the peer: the
/// page and anyone asking `next`, which is a forward pass under the
/// engine's lock. games_hub reaches `recent` across the app network, so
/// its poll is its own peer. Well above what those need and far below
/// what a scraper would cost.
const RATE_LIMIT: server_pal::RateLimit = server_pal::RateLimit {
    per_second: 20.0,
    burst: 40,
};

/// The routes under server_pal's stack; `stream` skips the Accept check,
/// the timeout and compression, which would each break a stream.
pub fn app(state: Arc<AppState>) -> Router {
    server_pal::router_builder()
        .rate_limit(Some(RATE_LIMIT))
        .route(STATE_PATH, get(get_state))
        .route(RECENT_PATH, get(get_recent))
        .route(NEXT_PATH, post(post_next))
        .stream_route(STREAM_PATH, get(get_stream))
        .build()
        .with_state(state)
}

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

    /// Scores one caddy line, records it, and fans it out to the stream.
    pub fn ingest(&self, line: &caddylog::CaddyLine) -> Arc<Event> {
        let engine = self.engine.lock().expect("engine lock");
        self.publish(engine, |engine| engine.ingest(line))
    }

    /// The same for an observation any other source made.
    pub fn observe(&self, observation: &crate::engine::Observation) -> Arc<Event> {
        let engine = self.engine.lock().expect("engine lock");
        self.publish(engine, |engine| engine.observe(observation))
    }

    /// Scores and fans out under one hold of the lock. The send has to be
    /// inside it: with two sources, releasing between scoring and sending
    /// lets the thread that got the later `seq` send first, and `id` on
    /// the stream is what a reconnecting page resumes `recent?after=`
    /// from — out of order it re-renders one event or drops another.
    fn publish(
        &self,
        mut engine: MutexGuard<'_, Engine>,
        score: impl FnOnce(&mut Engine) -> Arc<Event>,
    ) -> Arc<Event> {
        let event = score(&mut engine);
        drop(engine);
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

#[derive(Deserialize)]
pub struct NextRequest {
    context: Vec<String>,
}

#[derive(Serialize)]
struct Next {
    predictions: PerPredictor<Vec<Guess>>,
}

#[derive(Serialize)]
struct Refusal {
    error: String,
}

/// Both predictors' top five after a context of one to eight token names.
/// A body that is not that, or names a token the vocabulary lacks, is a
/// 400 whose body says which.
pub async fn post_next(
    State(state): State<Arc<AppState>>,
    body: Result<Json<NextRequest>, JsonRejection>,
) -> Response {
    let refuse = |error: String| (StatusCode::BAD_REQUEST, Json(Refusal { error })).into_response();
    let Json(request) = match body {
        Ok(body) => body,
        Err(rejection) => return refuse(rejection.body_text()),
    };
    // A candle forward under the engine's lock is real CPU; off the async
    // worker, so a question cannot stall the runtime the tailer and every
    // open stream share.
    let answer = tokio::task::spawn_blocking(move || {
        state
            .engine
            .lock()
            .expect("engine lock")
            .next(&request.context)
    })
    .await
    .expect("the engine answers without panicking");
    match answer {
        Ok(predictions) => Json(Next { predictions }).into_response(),
        Err(error) => refuse(error.to_string()),
    }
}

/// Server-sent events, one per request, `id` the sequence number so a
/// reconnecting page resumes through `recent?after=` from where it was.
/// The stream ends when the client falls `STREAM_BACKLOG` behind or after
/// `STREAM_LIFETIME`; either way the client reconnects and resumes.
pub async fn get_stream(State(state): State<Arc<AppState>>) -> Response {
    let Some(seat) = Seat::take(&state) else {
        return (StatusCode::SERVICE_UNAVAILABLE, "stream is full").into_response();
    };
    let events = BroadcastStream::new(state.events.subscribe())
        .take_while(Result::is_ok)
        .map(|item| -> Result<SseEvent, Infallible> {
            let event = item.expect("errors end the stream above");
            let data = serde_json::to_string(&*event).expect("an Event serializes");
            Ok(SseEvent::default().id(event.seq.to_string()).data(data))
        });
    Sse::new(Bounded::new(events, STREAM_LIFETIME, seat))
        .keep_alive(KeepAlive::default())
        .into_response()
}

/// A stream that ends at its deadline whatever its source has left. The
/// seat is given back the moment the stream ends, or when the client
/// drops it, whichever is first.
struct Bounded<T> {
    inner: Pin<Box<dyn Stream<Item = T> + Send>>,
    deadline: Pin<Box<Sleep>>,
    seat: Option<Seat>,
}

impl<T> Bounded<T> {
    fn new(inner: impl Stream<Item = T> + Send + 'static, lifetime: Duration, seat: Seat) -> Self {
        Self {
            inner: Box::pin(inner),
            deadline: Box::pin(tokio::time::sleep(lifetime)),
            seat: Some(seat),
        }
    }
}

impl<T> Stream for Bounded<T> {
    type Item = T;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<T>> {
        let next = if self.deadline.as_mut().poll(cx).is_ready() {
            Poll::Ready(None)
        } else {
            self.inner.as_mut().poll_next(cx)
        };
        if matches!(next, Poll::Ready(None)) {
            self.seat = None;
        }
        next
    }
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
    use std::net::SocketAddr;

    use axum::{
        body::Body,
        extract::ConnectInfo,
        http::{Request, header},
    };
    use http_body_util::BodyExt;
    use tower::ServiceExt;

    use super::*;
    use crate::engine::fixtures::*;

    /// The handlers without server_pal's stack, for the seat-cap test:
    /// the stack's limiter would refuse the 257th request from one peer
    /// before the cap could.
    fn bare(state: Arc<AppState>) -> Router {
        Router::new()
            .route(STREAM_PATH, get(get_stream))
            .with_state(state)
    }

    /// An engine whose net starts from a fixed point rather than the
    /// initializer's draw, so the pinned floats below move only when the
    /// arithmetic that produced them does.
    fn fresh() -> Arc<AppState> {
        AppState::new(crate::engine::fixtures::zeroed(), AppMetrics::new())
    }

    async fn body_text(response: Response) -> String {
        let bytes = response.into_body().collect().await.unwrap().to_bytes();
        String::from_utf8(bytes.to_vec()).unwrap()
    }

    /// As a client sends it: the stack wants a peer for the limiter and an
    /// Accept for the JSON routes; `EventSource` sends `text/event-stream`.
    async fn fetch(app: Router, uri: &str, accept: &str) -> Response {
        let peer: SocketAddr = "127.0.0.1:12345".parse().unwrap();
        let mut request = Request::builder()
            .uri(uri)
            .header(header::ACCEPT, accept)
            .body(Body::empty())
            .unwrap();
        request.extensions_mut().insert(ConnectInfo(peer));
        app.oneshot(request).await.unwrap()
    }

    async fn json(app: Router, uri: &str) -> Response {
        fetch(app, uri, "application/json").await
    }

    async fn post_json(app: Router, uri: &str, body: &'static str) -> Response {
        let peer: SocketAddr = "127.0.0.1:12345".parse().unwrap();
        let mut request = Request::builder()
            .method("POST")
            .uri(uri)
            .header(header::ACCEPT, "application/json")
            .header(header::CONTENT_TYPE, "application/json")
            .body(Body::from(body))
            .unwrap();
        request.extensions_mut().insert(ConnectInfo(peer));
        app.oneshot(request).await.unwrap()
    }

    async fn stream(app: Router) -> Response {
        fetch(app, STREAM_PATH, "text/event-stream").await
    }

    fn redirect(ts: f64) -> caddylog::CaddyLine {
        request(ts, "1.1.1.1", "GET", "/iili/v1/r/abc", 302, BROWSER)
    }

    // The consumer boundary games_hub polls from C++ (#1554): the raw
    // bytes, key names and order included. A rename on this side fails
    // here before it fails the lobby.
    #[tokio::test]
    async fn recent_is_pinned_on_the_wire() {
        let state = fresh();
        state.ingest(&redirect(1789500000.25));
        state.ingest(&redirect(1789500001.5));
        let response = json(app(state), "/deja/v1/recent?after=1").await;
        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(response.headers()[header::CONTENT_TYPE], "application/json");
        assert_eq!(
            body_text(response).await,
            r#"{"events":[{"seq":2,"ts":1789500001.5,"lane":0,"step":0,"context":["api.muchq.com GET /iili/v1/r/* 302 browser"],"actual":"api.muchq.com GET /iili/v1/r/* 302 browser","predictions":{"bigram":[],"net":[{"token":"api.muchq.com GET /iili/v1/r/* 302 browser","p":0.3377925157546997},{"token":"<unk>","p":0.33110377192497253}]},"surprise":{"bigram":1.0986122886681098,"net":1.085323452949524},"threshold":null,"verdict":"warmup","ewma_loss":{"bigram":0.0,"net":0.0},"vocab_size":3}]}"#
        );
    }

    // The "ask it" panel's boundary: a context of token names in, both
    // predictors' top five out, and a refusal that names the token.
    #[tokio::test]
    async fn next_is_pinned_on_the_wire() {
        let state = fresh();
        state.ingest(&redirect(1.0));
        state.ingest(&redirect(2.0));
        let before = state.engine.lock().unwrap().snapshot();
        let response = post_json(
            app(Arc::clone(&state)),
            NEXT_PATH,
            r#"{"context":["api.muchq.com GET /iili/v1/r/* 302 browser"]}"#,
        )
        .await;
        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(response.headers()[header::CONTENT_TYPE], "application/json");
        assert_eq!(
            body_text(response).await,
            r#"{"predictions":{"bigram":[{"token":"api.muchq.com GET /iili/v1/r/* 302 browser","p":0.8461538461538461}],"net":[{"token":"api.muchq.com GET /iili/v1/r/* 302 browser","p":0.34227943420410156},{"token":"<unk>","p":0.3288603127002716}]}}"#
        );
        assert_eq!(
            state.engine.lock().unwrap().snapshot(),
            before,
            "asking is not an event: the same weights, vocabulary and baselines"
        );
    }

    #[tokio::test]
    async fn next_refuses_a_bad_context_with_a_json_error() {
        let state = fresh();
        state.ingest(&redirect(1.0));
        let refused = |body: &'static str| post_json(app(Arc::clone(&state)), NEXT_PATH, body);
        let response = refused(r#"{"context":["api.muchq.com GET /nope 200 browser"]}"#).await;
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
        assert_eq!(response.headers()[header::CONTENT_TYPE], "application/json");
        assert_eq!(
            body_text(response).await,
            r#"{"error":"unknown token \"api.muchq.com GET /nope 200 browser\""}"#
        );
        let response = refused(r#"{"context":[]}"#).await;
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
        assert_eq!(
            body_text(response).await,
            r#"{"error":"context has 0 tokens; 1 to 8 are allowed"}"#
        );
        let response = refused(r#"{"context":["<bos>","<bos>","<bos>","<bos>","<bos>","<bos>","<bos>","<bos>","<bos>"]}"#).await;
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
        assert_eq!(
            body_text(response).await,
            r#"{"error":"context has 9 tokens; 1 to 8 are allowed"}"#
        );
        let response = refused(r#"{"context":"not a list"}"#).await;
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
        assert_eq!(response.headers()[header::CONTENT_TYPE], "application/json");
        assert!(body_text(response).await.starts_with(r#"{"error":""#));
    }

    #[tokio::test]
    async fn recent_defaults_to_everything_and_state_counts() {
        let state = fresh();
        state.ingest(&redirect(1.0));
        state.ingest(&redirect(2.0));
        let all: serde_json::Value = serde_json::from_str(
            &body_text(json(app(Arc::clone(&state)), RECENT_PATH).await).await,
        )
        .unwrap();
        assert_eq!(all["events"].as_array().unwrap().len(), 2);
        let none: serde_json::Value = serde_json::from_str(
            &body_text(json(app(Arc::clone(&state)), "/deja/v1/recent?after=2").await).await,
        )
        .unwrap();
        assert_eq!(none["events"].as_array().unwrap().len(), 0);
        let view = body_text(json(app(state), STATE_PATH).await).await;
        assert_eq!(
            view,
            r#"{"seq":2,"step":1,"vocab_size":3,"vocab_cap":2048,"warmup_needed":1000,"ewma_loss":{"bigram":1.0986122886681098,"net":1.085323452949524},"threshold":null,"anomalies":0,"novelties":1}"#
        );
    }

    // Through the whole stack: the JSON routes' Accept check does not
    // apply to the stream, and nothing between the handler and the client
    // holds a frame back.
    #[tokio::test]
    async fn the_stream_carries_each_event_with_its_sequence_as_the_id() {
        let state = fresh();
        let response = stream(app(Arc::clone(&state))).await;
        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(
            response.headers()[header::CONTENT_TYPE],
            "text/event-stream"
        );
        assert_eq!(state.subscribers.load(Ordering::SeqCst), 1);
        state.ingest(&redirect(1.0));
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

    // A subscriber that stops reading is not kept: once it is a backlog
    // behind, its stream ends and its seat is freed. The events it missed
    // are in the ring for the reconnect.
    #[tokio::test]
    async fn a_lagging_subscriber_is_ended_not_skipped_over() {
        let state = fresh();
        let response = stream(app(Arc::clone(&state))).await;
        for i in 0..=STREAM_BACKLOG {
            state.ingest(&redirect(i as f64));
        }
        let mut body = response.into_body().into_data_stream();
        assert!(body.next().await.is_none(), "the stream did not end");
        assert_eq!(state.subscribers.load(Ordering::SeqCst), 0);
        assert_eq!(
            state.engine.lock().unwrap().recent(0).len(),
            STREAM_BACKLOG + 1
        );
    }

    #[tokio::test(start_paused = true)]
    async fn a_stream_ends_at_its_lifetime_and_frees_its_seat() {
        let state = fresh();
        let response = stream(app(Arc::clone(&state))).await;
        let mut body = response.into_body().into_data_stream();
        state.ingest(&redirect(1.0));
        assert!(body.next().await.is_some());
        tokio::time::advance(STREAM_LIFETIME).await;
        state.ingest(&redirect(2.0));
        assert!(
            body.next().await.is_none(),
            "the stream outlived its lifetime"
        );
        assert_eq!(state.subscribers.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn the_stream_refuses_a_subscriber_past_the_cap() {
        let state = fresh();
        let mut held = Vec::new();
        for _ in 0..MAX_SUBSCRIBERS {
            let response = stream(bare(Arc::clone(&state))).await;
            assert_eq!(response.status(), StatusCode::OK);
            held.push(response);
        }
        let response = stream(bare(Arc::clone(&state))).await;
        assert_eq!(response.status(), StatusCode::SERVICE_UNAVAILABLE);
        held.pop();
        let response = stream(bare(state)).await;
        assert_eq!(
            response.status(),
            StatusCode::OK,
            "a dropped client frees its seat"
        );
    }
}
