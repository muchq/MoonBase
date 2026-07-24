use std::net::SocketAddr;
use std::sync::{Arc, OnceLock};

use axum::Router;
use axum::extract::{DefaultBodyLimit, Request, State};
use axum::http::{StatusCode, Uri};
use axum::middleware::Next;
use axum::response::Response;
use axum::routing::{MethodRouter, get};
use opentelemetry::KeyValue;
use opentelemetry::metrics::{Counter, Histogram, UpDownCounter};
use opentelemetry_otlp::{MetricExporter, WithExportConfig};
use opentelemetry_sdk::Resource;
use opentelemetry_sdk::metrics::{PeriodicReader, SdkMeterProvider};
use tokio::net::TcpListener;
use tower_governor::GovernorLayer;
use tower_governor::governor::GovernorConfigBuilder;
use std::env;
use std::time::Duration;
use tower_http::catch_panic::CatchPanicLayer;
use tower_http::compression::CompressionLayer;
use tower_http::limit::RequestBodyLimitLayer;
use tower_http::timeout::TimeoutLayer;
use tower_http::trace::TraceLayer;
use tower_http::validate_request::ValidateRequestHeaderLayer;

const DEFAULT_PORT: u16 = 8080;

static HTTP_REQUESTS: OnceLock<Counter<u64>> = OnceLock::new();
static HTTP_SUCCESS: OnceLock<Counter<u64>> = OnceLock::new();
static HTTP_FAILURE: OnceLock<Counter<u64>> = OnceLock::new();
static HTTP_ACTIVE: OnceLock<UpDownCounter<i64>> = OnceLock::new();
static HTTP_DURATION: OnceLock<Histogram<f64>> = OnceLock::new();

/// Initialise the global OTel meter provider when
/// OTEL_EXPORTER_OTLP_ENDPOINT is set; a no-op None otherwise. Callers
/// keep the returned provider alive for the process lifetime — dropping
/// it shuts down the exporter. Shared by every server_pal service so the
/// http_server_* instruments actually export.
pub fn init_otel() -> Option<SdkMeterProvider> {
    let endpoint = env::var("OTEL_EXPORTER_OTLP_ENDPOINT").ok()?;

    // reqwest is pinned with rustls-no-provider (workspace Cargo.toml), so
    // no default CryptoProvider is compiled in; the OTLP exporter's reqwest
    // client panics on its first export unless the process installs one.
    // Idempotent — Err means another caller already installed it.
    let _ = rustls::crypto::ring::default_provider().install_default();

    let exporter = match MetricExporter::builder()
        .with_http()
        .with_endpoint(format!("{}/v1/metrics", endpoint))
        .with_timeout(Duration::from_secs(5))
        .build()
    {
        Ok(exporter) => exporter,
        Err(e) => {
            eprintln!("warning: failed to create OTLP metric exporter: {e}; metrics disabled");
            return None;
        }
    };

    // Resource::builder() picks up OTEL_SERVICE_NAME and
    // OTEL_RESOURCE_ATTRIBUTES via the built-in EnvResourceDetector.
    let provider = SdkMeterProvider::builder()
        .with_reader(PeriodicReader::builder(exporter).build())
        .with_resource(Resource::builder().build())
        .build();

    opentelemetry::global::set_meter_provider(provider.clone());
    eprintln!("OTel metrics initialised (endpoint: {endpoint})");
    Some(provider)
}

fn http_counter(
    cell: &'static OnceLock<Counter<u64>>,
    name: &'static str,
    desc: &'static str,
) -> &'static Counter<u64> {
    cell.get_or_init(|| {
        opentelemetry::global::meter("http_server")
            .u64_counter(name)
            .with_description(desc)
            .build()
    })
}

/// In-flight gauge as a drop guard: a manual decrement after the await
/// never runs when the request future is cancelled (client disconnect),
/// and the gauge would drift upward forever.
struct ActiveRequest {
    attrs: [KeyValue; 2],
}

impl ActiveRequest {
    fn start(attrs: [KeyValue; 2]) -> Self {
        Self::counter().add(1, &attrs);
        Self { attrs }
    }

    fn counter() -> &'static UpDownCounter<i64> {
        HTTP_ACTIVE.get_or_init(|| {
            opentelemetry::global::meter("http_server")
                .i64_up_down_counter("http_server_requests_active_gauge")
                .with_description("HTTP requests currently in flight")
                .build()
        })
    }
}

impl Drop for ActiveRequest {
    fn drop(&mut self) {
        Self::counter().add(-1, &self.attrs);
    }
}

// Instrument names mirror the C++ aura/futility http_server_* family
// (requests, success/failure, active gauge, microseconds histogram), so
// prom_proxy's standard service block reads every language the same way.
async fn http_metrics_middleware(req: Request, next: Next) -> Response {
    let start = std::time::Instant::now();
    let method = req.method().as_str().to_string();
    let service_name = env::var("OTEL_SERVICE_NAME").unwrap_or_default();
    let attrs = [
        KeyValue::new("http_method", method),
        KeyValue::new("service_name", service_name),
    ];

    http_counter(&HTTP_REQUESTS, "http_server_requests", "HTTP requests received").add(1, &attrs);
    let _active = ActiveRequest::start(attrs.clone());

    let resp = next.run(req).await;

    let duration_us = start.elapsed().as_micros() as f64;
    let status = resp.status().as_u16();

    if status < 400 {
        http_counter(
            &HTTP_SUCCESS,
            "http_server_requests_success",
            "HTTP requests completed successfully (2xx–3xx)",
        )
        .add(1, &attrs);
    } else {
        http_counter(
            &HTTP_FAILURE,
            "http_server_requests_failure",
            "HTTP requests that returned 4xx or 5xx",
        )
        .add(1, &attrs);
    }

    HTTP_DURATION
        .get_or_init(|| {
            opentelemetry::global::meter("http_server")
                .f64_histogram("http_server_request_duration_microseconds")
                .with_description("HTTP request duration in microseconds")
                .build()
        })
        .record(duration_us, &attrs);

    resp
}

pub struct RateLimit {
    pub per_second: u64,
    pub burst: u32,
}

const DEFAULT_RATE_LIMIT: RateLimit = RateLimit {
    per_second: 100,
    burst: 200,
};

pub fn listen_addr_pal() -> String {
    let port = env::var("PORT")
        .ok()
        .and_then(|p| p.parse::<u16>().ok())
        .unwrap_or(DEFAULT_PORT);

    format!("0.0.0.0:{}", &port)
}

async fn fallback(_: Uri) -> (StatusCode, String) {
    (StatusCode::NOT_FOUND, "Not Found".to_string())
}

pub struct RouterBuilder<S: Clone + Send + Sync + 'static> {
    router: Router<S>,
    rate_limit: Option<RateLimit>,
}

pub fn router_builder<S: Clone + Send + Sync + 'static>() -> RouterBuilder<S> {
    RouterBuilder {
        router: Router::new(),
        rate_limit: Some(DEFAULT_RATE_LIMIT),
    }
}

impl<S: Clone + Send + Sync + 'static> RouterBuilder<S> {
    pub fn route(mut self, path: &str, method_router: MethodRouter<S>) -> Self {
        self.router = self.router.route(path, method_router);
        self
    }

    /// Override the per-IP rate limit. Use `None` to disable entirely.
    pub fn rate_limit(mut self, limit: Option<RateLimit>) -> Self {
        self.rate_limit = limit;
        self
    }

    pub fn build(self) -> Router<S> {
        let router = self
            .router
            .route("/health", get(|_: State<S>| async { "Ok" }))
            .fallback(fallback)
            .layer(TraceLayer::new_for_http())
            .layer(DefaultBodyLimit::disable())
            .layer(RequestBodyLimitLayer::new(4 * 1024 * 1024))
            .layer(CompressionLayer::new())
            .layer(ValidateRequestHeaderLayer::accept("application/json"))
            .layer(TimeoutLayer::with_status_code(
                StatusCode::REQUEST_TIMEOUT,
                Duration::from_secs(10),
            ))
            .layer(CatchPanicLayer::new());

        let router = if let Some(RateLimit { per_second, burst }) = self.rate_limit {
            let config = Arc::new(
                GovernorConfigBuilder::default()
                    .per_second(per_second)
                    .burst_size(burst)
                    .finish()
                    .unwrap(),
            );
            router.layer(GovernorLayer::new(config))
        } else {
            router
        };

        // HTTP metrics middleware sits outside rate-limiting so rate-limited
        // requests (429) are also counted as failures.
        router.layer(axum::middleware::from_fn(http_metrics_middleware))
    }
}

/// Bind and serve a router, enabling per-IP rate limiting via connect_info.
pub async fn serve(router: Router<()>, addr: &str) {
    let listener = TcpListener::bind(addr).await.unwrap();
    axum::serve(
        listener,
        router.into_make_service_with_connect_info::<SocketAddr>(),
    )
    .await
    .unwrap();
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::body::Body;
    use axum::extract::ConnectInfo;
    use axum::http::{Request, StatusCode};
    use tower::util::ServiceExt;

    #[derive(Clone)]
    struct NoState;

    // Inject a mock peer IP so GovernorLayer's PeerIpKeyExtractor can extract it.
    fn make_request() -> Request<Body> {
        let peer: SocketAddr = "127.0.0.1:12345".parse().unwrap();
        let mut req = Request::builder()
            .method("GET")
            .uri("/health")
            .header("Accept", "application/json")
            .body(Body::empty())
            .unwrap();
        req.extensions_mut().insert(ConnectInfo(peer));
        req
    }

    #[tokio::test]
    async fn rate_limiter_blocks_after_burst() {
        // 1 req/s per IP, burst of 2
        let app = router_builder::<NoState>()
            .rate_limit(Some(RateLimit { per_second: 1, burst: 2 }))
            .build()
            .with_state(NoState);

        // First two requests should pass (burst of 2)
        for _ in 0..2 {
            let resp = app.clone().oneshot(make_request()).await.unwrap();
            assert_ne!(resp.status(), StatusCode::TOO_MANY_REQUESTS);
        }

        // Third request immediately after should be rate-limited
        let resp = app.oneshot(make_request()).await.unwrap();
        assert_eq!(resp.status(), StatusCode::TOO_MANY_REQUESTS);
    }

    #[tokio::test]
    async fn rate_limiter_can_be_disabled() {
        let app = router_builder::<NoState>()
            .rate_limit(None)
            .build()
            .with_state(NoState);

        for _ in 0..20 {
            let resp = app.clone().oneshot(make_request()).await.unwrap();
            assert_ne!(resp.status(), StatusCode::TOO_MANY_REQUESTS);
        }
    }
}
