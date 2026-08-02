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
use opentelemetry_otlp::{MetricExporter, WithExportConfig, WithHttpConfig};
use opentelemetry_sdk::Resource;
use opentelemetry_sdk::metrics::{Aggregation, Instrument, PeriodicReader, SdkMeterProvider, Stream};
use std::env;
use std::time::Duration;
use tokio::net::TcpListener;
use tower_governor::GovernorLayer;
use tower_governor::governor::GovernorConfigBuilder;
use tower_http::catch_panic::CatchPanicLayer;
use tower_http::compression::CompressionLayer;
use tower_http::limit::RequestBodyLimitLayer;
use tower_http::timeout::TimeoutLayer;
use tower_http::trace::TraceLayer;
use tower_http::validate_request::ValidateRequestHeaderLayer;

const DEFAULT_PORT: u16 = 8080;

/// Explicit bucket boundaries, in microseconds, for latency histograms —
/// 100µs to 10s.
///
/// Without a view the SDK applies its own defaults (0, 5, 10, ... 10000),
/// which are shaped for milliseconds. `http_server_request_duration_
/// microseconds` records microseconds, so that top finite bucket meant 10ms:
/// every slower request landed in +Inf, and `histogram_quantile` answers the
/// highest finite bound when the rank falls there, so p95 read a flat 10000
/// however slow the service actually was (#1286). Seen in production on
/// portrait at a real p95 near a second (#1287).
///
/// yodel (Java, `HttpServerMetrics.BUCKET_BOUNDS`) and futility/otel (C++,
/// `kHttpLatencyBucketBoundsMicros`) declare this same set. prom_proxy runs one
/// `histogram_quantile` expression across all three languages, which only
/// compares like with like if the layouts match — so the three move together or
/// not at all. `//domains/platform/libs/otel_contract` pins them equal.
pub const HTTP_LATENCY_BUCKET_BOUNDS_MICROS: [f64; 15] = [
    100.0, 250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0, 25000.0, 50000.0, 100000.0, 250000.0,
    500000.0, 1000000.0, 2500000.0, 10000000.0,
];

static HTTP_REQUESTS: OnceLock<Counter<u64>> = OnceLock::new();
static HTTP_SUCCESS: OnceLock<Counter<u64>> = OnceLock::new();
static HTTP_FAILURE: OnceLock<Counter<u64>> = OnceLock::new();
static HTTP_ACTIVE: OnceLock<UpDownCounter<i64>> = OnceLock::new();
static HTTP_DURATION: OnceLock<Histogram<f64>> = OnceLock::new();

/// Whether an instrument of this name should get the explicit latency buckets.
///
/// A free function rather than an inline condition purely so it can be tested:
/// `Instrument` has no public constructor, so a test cannot drive the view
/// closure itself, and the predicate is the half of that closure with a
/// decision in it.
///
/// The suffix is the contract. `RecordLatency`-equivalent instruments end in
/// `_microseconds` and want a layout reaching 10s; distributions over counts
/// keep their bare names and want the SDK defaults, which start near zero.
fn wants_latency_buckets(instrument_name: &str) -> bool {
    instrument_name.ends_with("_microseconds")
}

/// Initialise the global OTel meter provider when
/// OTEL_EXPORTER_OTLP_ENDPOINT is set; a no-op None otherwise. Callers
/// keep the returned provider alive for the process lifetime — dropping
/// it shuts down the exporter. Shared by every server_pal service so the
/// http_server_* instruments actually export.
pub fn init_otel() -> Option<SdkMeterProvider> {
    let endpoint = env::var("OTEL_EXPORTER_OTLP_ENDPOINT").ok()?;

    // reqwest is pinned with rustls-no-provider (workspace Cargo.toml), so
    // no default CryptoProvider is compiled in; installing one lets reqwest
    // build its TLS client below. Idempotent — Err means already installed.
    let _ = rustls::crypto::ring::default_provider().install_default();

    // reqwest 0.13 dropped its webpki-roots feature and forces
    // rustls-platform-verifier, which reads the system trust store — absent
    // in our minimal container images. Left to its own devices the OTLP
    // exporter builds a default reqwest client that fails ("No CA
    // certificates were loaded from the system") and panics. Build the
    // client ourselves with Mozilla's roots bundled into the binary
    // (tls_certs_only, reqwest's stable roots API), so it needs nothing from
    // the host.
    // The blocking client spins up its own runtime, so it must be built off
    // the async main thread (opentelemetry-otlp's default path does the same).
    let http_client = match std::thread::spawn(|| {
        let roots = webpki_root_certs::TLS_SERVER_ROOT_CERTS
            .iter()
            .filter_map(|der| reqwest::Certificate::from_der(der).ok());
        reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(5))
            .tls_certs_only(roots)
            .build()
    })
    .join()
    {
        Ok(Ok(client)) => client,
        Ok(Err(e)) => {
            eprintln!("warning: failed to build OTLP http client: {e}; metrics disabled");
            return None;
        }
        Err(_) => {
            eprintln!("warning: OTLP http client build panicked; metrics disabled");
            return None;
        }
    };

    let exporter = match MetricExporter::builder()
        .with_http()
        .with_http_client(http_client)
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

    // Matched on the suffix rather than on the one HTTP instrument name: any
    // histogram whose name says it holds microseconds wants this layout, and
    // pinning the single name would leave the next one to rediscover #1286.
    // Nothing here records a non-latency quantity under that suffix.
    //
    // build() returns Err only for a malformed stream (unsorted or empty
    // boundaries), which for a const array is a compile-time-shaped mistake
    // rather than a runtime one. .ok() therefore reads as "no view" — the
    // silent default-bucket path this exists to eliminate — so it is
    // deliberately not used: a broken constant should be loud.
    let latency_buckets = |i: &Instrument| {
        if !wants_latency_buckets(i.name()) {
            return None;
        }
        Some(
            Stream::builder()
                .with_aggregation(Aggregation::ExplicitBucketHistogram {
                    boundaries: HTTP_LATENCY_BUCKET_BOUNDS_MICROS.to_vec(),
                    record_min_max: true,
                })
                .build()
                .expect("HTTP_LATENCY_BUCKET_BOUNDS_MICROS must be a valid histogram layout"),
        )
    };

    // Resource::builder() picks up OTEL_SERVICE_NAME and
    // OTEL_RESOURCE_ATTRIBUTES via the built-in EnvResourceDetector.
    let provider = SdkMeterProvider::builder()
        .with_reader(PeriodicReader::builder(exporter).build())
        .with_resource(Resource::builder().build())
        .with_view(latency_buckets)
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

    http_counter(
        &HTTP_REQUESTS,
        "http_server_requests",
        "HTTP requests received",
    )
    .add(1, &attrs);
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
            .rate_limit(Some(RateLimit {
                per_second: 1,
                burst: 2,
            }))
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

#[cfg(test)]
mod latency_bucket_tests {
    use super::*;

    /// The predicate behind `init_otel`'s view.
    ///
    /// `//domains/platform/libs/otel_contract` pins this rail's bucket constant
    /// equal to the Java and C++ ones, but a constant is only half the fix —
    /// nothing applies it except the view, and a view whose predicate stops
    /// matching leaves the constant correct and the histograms on the SDK's
    /// millisecond defaults, which is #1286 exactly.
    #[test]
    fn latency_instruments_are_selected_and_others_are_not() {
        assert!(wants_latency_buckets(
            "http_server_request_duration_microseconds"
        ));
        // portrait's render timer, on the C++ rail, carries the same suffix and
        // the same defect; narrowing this to an http_server_ prefix would put
        // that class of instrument back on the defaults (#1287).
        assert!(wants_latency_buckets("trace_request_duration_microseconds"));

        // Distributions keep their bare names. Against bounds starting at 100µs
        // every one of these — spheres, lights, rows — collapses into bucket 0.
        assert!(!wants_latency_buckets("scene_sphere_count"));
        assert!(!wants_latency_buckets("chat_catch_up_rows"));
        assert!(!wants_latency_buckets("http_server_requests_total"));

        // Suffix, not substring. Both of these contain the token and neither
        // ends with it, so relaxing `ends_with` to `contains` fails here rather
        // than silently widening the view over instruments it was never meant
        // to reshape.
        assert!(!wants_latency_buckets("request_microseconds_histogram"));
        assert!(!wants_latency_buckets("microseconds_elapsed_total"));
    }

    /// The constant itself is shaped for microseconds, ascending, and reaches
    /// far enough that a one-second request is not in the overflow bucket.
    ///
    /// otel_contract asserts the same properties by parsing this file as text;
    /// this asserts them against the compiled value, so a mismatch between what
    /// the file says and what rustc sees cannot hide.
    #[test]
    fn the_bounds_are_ascending_and_reach_ten_seconds() {
        let bounds = HTTP_LATENCY_BUCKET_BOUNDS_MICROS;
        assert!(bounds[0] > 0.0);
        for pair in bounds.windows(2) {
            assert!(pair[1] > pair[0], "bounds must ascend: {pair:?}");
        }
        assert_eq!(*bounds.last().unwrap(), 10_000_000.0);
        assert!(
            bounds.iter().any(|b| *b >= 1_000_000.0),
            "a one-second request must land in a finite bucket"
        );
    }
}
