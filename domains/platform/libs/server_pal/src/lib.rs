use std::net::SocketAddr;
use std::sync::Arc;

use axum::Router;
use axum::extract::{DefaultBodyLimit, MatchedPath, Request, State};
use axum::http::{StatusCode, Uri};
use axum::middleware::Next;
use axum::response::Response;
use axum::routing::{MethodRouter, get};
use opentelemetry::KeyValue;
use opentelemetry::metrics::{Counter, Histogram, Meter, UpDownCounter};
use opentelemetry_otlp::{MetricExporter, WithExportConfig, WithHttpConfig};
use opentelemetry_sdk::Resource;
use opentelemetry_sdk::metrics::{
    Aggregation, Instrument, PeriodicReader, SdkMeterProvider, Stream,
};
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

/// The route label for a request no route ever matched (404s, probes for
/// paths that don't exist). A fixed sentinel rather than the raw path, so
/// scanners cannot mint unbounded series — the same rule, and the same
/// spelling, as yodel's `HttpServerMetricsFilter.UNMATCHED_ROUTE` and
/// aura's `kUnmatchedRoute` (#1304, #1303).
pub const UNMATCHED_ROUTE: &str = "unmatched";

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

/// The SDK view applying `HTTP_LATENCY_BUCKET_BOUNDS_MICROS` to latency
/// histograms. Matched on the suffix rather than on the one HTTP instrument
/// name: any histogram whose name says it holds microseconds wants this
/// layout, and pinning the single name would leave the next one to rediscover
/// #1286. Nothing here records a non-latency quantity under that suffix.
///
/// A free function rather than a closure inside `init_otel` so a test can
/// register it on its own provider and read the exported bucket bounds back:
/// the constant is pinned by `//domains/platform/libs/otel_contract`, but a
/// constant nothing applies is exactly the bug #1286 describes.
///
/// `build()` returns Err only for a malformed stream (unsorted or empty
/// boundaries), which for a const array is a compile-time-shaped mistake
/// rather than a runtime one. `.ok()` therefore reads as "no view" — the
/// silent default-bucket path this exists to eliminate — so it is
/// deliberately not used: a broken constant should be loud.
fn latency_bucket_view(instrument: &Instrument) -> Option<Stream> {
    if !wants_latency_buckets(instrument.name()) {
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

    // Resource::builder() picks up OTEL_SERVICE_NAME and
    // OTEL_RESOURCE_ATTRIBUTES via the built-in EnvResourceDetector.
    let provider = SdkMeterProvider::builder()
        .with_reader(PeriodicReader::builder(exporter).build())
        .with_resource(Resource::builder().build())
        .with_view(latency_bucket_view)
        .build();

    opentelemetry::global::set_meter_provider(provider.clone());
    eprintln!("OTel metrics initialised (endpoint: {endpoint})");
    Some(provider)
}

/// The five shared serving instruments, resolved once per router at `build`
/// time rather than lazily from a process-wide static: a static binds to
/// whichever meter provider was global at first use, which no test can
/// control and only one test per process could ever win. Instrument names
/// mirror the C++ aura/futility http_server_* family (requests,
/// success/failure, active gauge, microseconds histogram), so prom_proxy's
/// standard service block reads every language the same way.
struct HttpInstruments {
    requests: Counter<u64>,
    success: Counter<u64>,
    failure: Counter<u64>,
    active: UpDownCounter<i64>,
    duration: Histogram<f64>,
    service_name: String,
}

impl HttpInstruments {
    fn from_global() -> Arc<Self> {
        Self::new(
            &opentelemetry::global::meter("http_server"),
            env::var("OTEL_SERVICE_NAME").unwrap_or_default(),
        )
    }

    // The descriptions are a cross-language contract: the collector merges
    // series by instrument name across services and keeps the first
    // description it sees, logging a conflict for every later one that
    // disagrees. The success range is spelled with an ASCII hyphen, matching
    // yodel and futility — an en-dash reads identically here and exports as
    // a different string. //domains/platform/libs/otel_contract pins these
    // equal to the other rails', and pins that no instrument declares a
    // unit: the Prometheus exporter folds a unit into the metric *name*
    // (#1294), which would silently fork every series off the dashboards.
    fn new(meter: &Meter, service_name: String) -> Arc<Self> {
        Arc::new(Self {
            requests: meter
                .u64_counter("http_server_requests")
                .with_description("HTTP requests received")
                .build(),
            success: meter
                .u64_counter("http_server_requests_success")
                .with_description("HTTP requests completed successfully (2xx-3xx)")
                .build(),
            failure: meter
                .u64_counter("http_server_requests_failure")
                .with_description("HTTP requests that returned 4xx or 5xx")
                .build(),
            active: meter
                .i64_up_down_counter("http_server_requests_active_gauge")
                .with_description("HTTP requests currently in flight")
                .build(),
            duration: meter
                .f64_histogram("http_server_request_duration_microseconds")
                .with_description("HTTP request duration in microseconds")
                .build(),
            service_name,
        })
    }
}

/// In-flight gauge plus the abandoned-request count, as a drop guard: the
/// code after the `next.run` await never executes when the request future is
/// cancelled (client disconnect), so a manual gauge decrement would drift
/// upward forever — and the request would otherwise vanish from
/// `http_server_requests` entirely, now that the counter moves at completion
/// to carry the route (#1304). An abandoned request was real load: it counts,
/// with its route, and records no outcome and no duration — the same contract
/// yodel documents on `HttpServerMetrics.recordRequestAbandoned`.
struct RequestGuard {
    instruments: Arc<HttpInstruments>,
    gauge_attrs: [KeyValue; 2],
    route_attrs: [KeyValue; 3],
    completed: bool,
}

impl RequestGuard {
    fn start(
        instruments: Arc<HttpInstruments>,
        gauge_attrs: [KeyValue; 2],
        route_attrs: [KeyValue; 3],
    ) -> Self {
        instruments.active.add(1, &gauge_attrs);
        Self {
            instruments,
            gauge_attrs,
            route_attrs,
            completed: false,
        }
    }

    /// Ends the in-flight window and hands the route attributes back to the
    /// completion path, which records the counters itself. Consumes the guard
    /// so the abandonment arm below cannot also fire.
    fn complete(mut self) -> [KeyValue; 3] {
        self.completed = true;
        let route_attrs = self.route_attrs.clone();
        drop(self);
        route_attrs
    }
}

impl Drop for RequestGuard {
    fn drop(&mut self) {
        self.instruments.active.add(-1, &self.gauge_attrs);
        if !self.completed {
            self.instruments.requests.add(1, &self.route_attrs);
        }
    }
}

// The counters and the histogram move at completion, where the status is
// known and the route can ride along; only the in-flight gauge moves at
// request start, keyed by method and service alone — the same recording
// contract yodel documents on HttpServerMetrics (#1303, #1304). The
// `requests` counter therefore counts completed-or-abandoned requests rather
// than started ones: the same totals, observed a request-duration later.
async fn http_metrics_middleware(
    State(instruments): State<Arc<HttpInstruments>>,
    req: Request,
    next: Next,
) -> Response {
    let start = std::time::Instant::now();
    let method = req.method().as_str().to_string();
    // Router::layer middleware runs after routing, so the matched route
    // template ("/widgets/{id}", never the raw path) is already in the
    // request extensions here. The fallback leaves it absent, and mapping
    // that to a fixed sentinel is what keeps the label bounded: a scanner's
    // paths all collapse into one series instead of minting one each.
    let route = req
        .extensions()
        .get::<MatchedPath>()
        .map(|p| p.as_str().to_string())
        .unwrap_or_else(|| UNMATCHED_ROUTE.to_string());

    let gauge_attrs = [
        KeyValue::new("http_method", method.clone()),
        KeyValue::new("service_name", instruments.service_name.clone()),
    ];
    let route_attrs = [
        KeyValue::new("http_method", method),
        KeyValue::new("route", route),
        KeyValue::new("service_name", instruments.service_name.clone()),
    ];

    let guard = RequestGuard::start(instruments.clone(), gauge_attrs, route_attrs);

    let resp = next.run(req).await;

    let route_attrs = guard.complete();
    let duration_us = start.elapsed().as_micros() as f64;
    let status = resp.status().as_u16();

    instruments.requests.add(1, &route_attrs);
    if status < 400 {
        instruments.success.add(1, &route_attrs);
    } else {
        instruments.failure.add(1, &route_attrs);
    }
    instruments.duration.record(duration_us, &route_attrs);

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

    /// Composes the router with the production middleware stack. Resolves the
    /// http_server_* instruments from the global meter provider *now*, so
    /// call `init_otel` before `build` (every service main does) — a router
    /// built first binds its instruments to the no-op default and serves
    /// fine while exporting nothing.
    pub fn build(self) -> Router<S> {
        self.build_with(HttpInstruments::from_global())
    }

    fn build_with(self, instruments: Arc<HttpInstruments>) -> Router<S> {
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
        router.layer(axum::middleware::from_fn_with_state(
            instruments,
            http_metrics_middleware,
        ))
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

// The #1304 guard shape, through the real middleware and a real SDK pipeline:
// requests routed through `build_with` record into a per-test provider, so
// these tests read exported data points — names, label sets, values, bucket
// layouts, units — rather than trusting the recording code to mean what it
// says. Nothing here touches the global meter provider, so the suite stays
// parallel-safe.
#[cfg(test)]
mod http_metrics_label_tests {
    use super::*;
    use axum::body::Body;
    use axum::http::Request as TestRequest;
    use axum::routing::get;
    use opentelemetry::metrics::MeterProvider as _;
    use opentelemetry_sdk::metrics::InMemoryMetricExporter;
    use opentelemetry_sdk::metrics::data::{AggregatedMetrics, MetricData, ResourceMetrics};
    use tower::util::ServiceExt;

    #[derive(Clone)]
    struct NoState;

    const TEST_SERVICE: &str = "pal-under-test";

    struct Rig {
        provider: SdkMeterProvider,
        exporter: InMemoryMetricExporter,
        router: Router<()>,
    }

    /// A router over an isolated provider, with one templated route so the
    /// matched-template-vs-raw-path distinction is observable, and one
    /// hanging route so a test can abandon a request mid-flight.
    fn rig() -> Rig {
        let exporter = InMemoryMetricExporter::default();
        let provider = SdkMeterProvider::builder()
            .with_reader(PeriodicReader::builder(exporter.clone()).build())
            .with_view(latency_bucket_view)
            .build();
        let instruments =
            HttpInstruments::new(&provider.meter("http_server"), TEST_SERVICE.to_string());
        let router = router_builder::<NoState>()
            .rate_limit(None)
            .route("/widgets/{id}", get(|_: State<NoState>| async { "w" }))
            .route(
                "/hang",
                get(|_: State<NoState>| async { std::future::pending::<String>().await }),
            )
            .build_with(instruments)
            .with_state(NoState);
        Rig {
            provider,
            exporter,
            router,
        }
    }

    fn request(method: &str, path: &str) -> TestRequest<Body> {
        TestRequest::builder()
            .method(method)
            .uri(path)
            .header("Accept", "application/json")
            .body(Body::empty())
            .unwrap()
    }

    async fn send(rig: &Rig, method: &str, path: &str) -> StatusCode {
        rig.router
            .clone()
            .oneshot(request(method, path))
            .await
            .unwrap()
            .status()
    }

    /// The last (cumulative) export after a flush.
    fn exported(rig: &Rig) -> ResourceMetrics {
        rig.provider.force_flush().expect("force_flush failed");
        rig.exporter
            .get_finished_metrics()
            .expect("get_finished_metrics failed")
            .pop()
            .expect("nothing was exported")
    }

    fn sorted_attrs<'a>(attrs: impl Iterator<Item = &'a KeyValue>) -> Vec<(String, String)> {
        let mut out: Vec<_> = attrs
            .map(|kv| (kv.key.to_string(), kv.value.as_str().to_string()))
            .collect();
        out.sort();
        out
    }

    /// (sorted attribute set, value) per data point of the named u64 sum;
    /// empty when the instrument never recorded (the SDK exports nothing for
    /// an instrument with no measurements).
    fn u64_sum_points(rm: &ResourceMetrics, name: &str) -> Vec<(Vec<(String, String)>, u64)> {
        let mut out = vec![];
        for scope in rm.scope_metrics() {
            for metric in scope.metrics() {
                if metric.name() != name {
                    continue;
                }
                if let AggregatedMetrics::U64(MetricData::Sum(sum)) = metric.data() {
                    for dp in sum.data_points() {
                        out.push((sorted_attrs(dp.attributes()), dp.value()));
                    }
                }
            }
        }
        out.sort();
        out
    }

    fn i64_sum_points(rm: &ResourceMetrics, name: &str) -> Vec<(Vec<(String, String)>, i64)> {
        let mut out = vec![];
        for scope in rm.scope_metrics() {
            for metric in scope.metrics() {
                if metric.name() != name {
                    continue;
                }
                if let AggregatedMetrics::I64(MetricData::Sum(sum)) = metric.data() {
                    for dp in sum.data_points() {
                        out.push((sorted_attrs(dp.attributes()), dp.value()));
                    }
                }
            }
        }
        out.sort();
        out
    }

    fn route_attrs(method: &str, route: &str) -> Vec<(String, String)> {
        vec![
            ("http_method".to_string(), method.to_string()),
            ("route".to_string(), route.to_string()),
            ("service_name".to_string(), TEST_SERVICE.to_string()),
        ]
    }

    fn gauge_attrs(method: &str) -> Vec<(String, String)> {
        vec![
            ("http_method".to_string(), method.to_string()),
            ("service_name".to_string(), TEST_SERVICE.to_string()),
        ]
    }

    /// The guard #1304 asks for: the request counter carries the matched
    /// route template — not the raw path — and the in-flight gauge carries no
    /// route at all.
    #[tokio::test]
    async fn the_request_counter_carries_the_matched_route_and_the_gauge_does_not() {
        let rig = rig();
        assert_eq!(send(&rig, "GET", "/widgets/7").await, StatusCode::OK);

        let rm = exported(&rig);
        let expected = route_attrs("GET", "/widgets/{id}");
        assert_eq!(
            u64_sum_points(&rm, "http_server_requests"),
            vec![(expected.clone(), 1)],
            "the raw path /widgets/7 must not appear; the label is the matched template"
        );
        assert_eq!(
            u64_sum_points(&rm, "http_server_requests_success"),
            vec![(expected, 1)]
        );

        // The gauge moves at request start, before routing's answer matters
        // for anything it reports, and stays keyed by method and service
        // alone; zero because the request has completed. A route label here
        // would make prom_proxy's negative matcher subtract in-flight probes
        // on some rails and not others.
        assert_eq!(
            i64_sum_points(&rm, "http_server_requests_active_gauge"),
            vec![(gauge_attrs("GET"), 0)]
        );
    }

    /// Boundedness (the #1305 shape, on this rail): scanner paths share one
    /// sentinel series, and the health route keeps the literal that
    /// prom_proxy's probeFilter subtracts and its Probes tiles select.
    #[tokio::test]
    async fn unmatched_paths_share_one_sentinel_series_and_health_keeps_its_literal() {
        let rig = rig();
        assert_eq!(send(&rig, "GET", "/health").await, StatusCode::OK);
        for path in ["/wp-login.php", "/admin/config", "/widgets/7/nope"] {
            assert_eq!(send(&rig, "GET", path).await, StatusCode::NOT_FOUND);
        }

        let rm = exported(&rig);
        let requests = u64_sum_points(&rm, "http_server_requests");
        assert!(
            requests.contains(&(route_attrs("GET", "/health"), 1)),
            "probe traffic must land under exactly route=\"/health\": {requests:?}"
        );
        assert!(
            requests.contains(&(route_attrs("GET", UNMATCHED_ROUTE), 3)),
            "unmatched requests must share the sentinel: {requests:?}"
        );
        assert_eq!(
            requests.len(),
            2,
            "a scanner must not mint a series per path: {requests:?}"
        );

        // 404s are failures, under the same sentinel.
        assert_eq!(
            u64_sum_points(&rm, "http_server_requests_failure"),
            vec![(route_attrs("GET", UNMATCHED_ROUTE), 3)]
        );
    }

    /// A cancelled request (client gone before the response) still counts as
    /// a request — with its route — but records no outcome and no duration,
    /// and the gauge drains. Matches yodel's recordRequestAbandoned contract;
    /// the counter moving at completion (#1304) is what makes this case need
    /// its own arm in the drop guard.
    #[tokio::test]
    async fn an_abandoned_request_counts_with_its_route_and_records_no_outcome() {
        let rig = rig();
        let hung = tokio::time::timeout(
            Duration::from_millis(50),
            rig.router.clone().oneshot(request("GET", "/hang")),
        )
        .await;
        assert!(hung.is_err(), "the hanging handler completed a response");

        let rm = exported(&rig);
        assert_eq!(
            u64_sum_points(&rm, "http_server_requests"),
            vec![(route_attrs("GET", "/hang"), 1)]
        );
        assert_eq!(u64_sum_points(&rm, "http_server_requests_success"), vec![]);
        assert_eq!(u64_sum_points(&rm, "http_server_requests_failure"), vec![]);
        assert_eq!(
            i64_sum_points(&rm, "http_server_requests_active_gauge"),
            vec![(gauge_attrs("GET"), 0)],
            "the drop guard must still drain the gauge"
        );
    }

    /// No shared instrument declares a unit (#1294). The collector's
    /// Prometheus exporter folds a non-empty unit into the metric *name*
    /// (http_server_requests_total would become
    /// http_server_requests_microseconds_total), logging no conflict — the
    /// series just forks, and prom_proxy selects by literal name, so the
    /// service silently vanishes from every dashboard panel.
    #[tokio::test]
    async fn no_shared_instrument_declares_a_unit() {
        let rig = rig();
        assert_eq!(send(&rig, "GET", "/widgets/7").await, StatusCode::OK);
        assert_eq!(send(&rig, "GET", "/nope").await, StatusCode::NOT_FOUND);

        let rm = exported(&rig);
        let mut seen = vec![];
        for scope in rm.scope_metrics() {
            for metric in scope.metrics() {
                seen.push(metric.name().to_string());
                assert_eq!(
                    metric.unit(),
                    "",
                    "{} declares a unit; the collector folds it into the metric name (#1294)",
                    metric.name()
                );
            }
        }
        for name in [
            "http_server_requests",
            "http_server_requests_success",
            "http_server_requests_failure",
            "http_server_requests_active_gauge",
            "http_server_request_duration_microseconds",
        ] {
            assert!(
                seen.contains(&name.to_string()),
                "{name} was never exported, so nothing pinned its unit; seen: {seen:?}"
            );
        }
    }

    /// The exported histogram actually carries HTTP_LATENCY_BUCKET_BOUNDS_
    /// MICROS. otel_contract pins the constant equal across the three rails,
    /// and latency_instruments_are_selected_and_others_are_not pins the view
    /// predicate — this closes the remaining gap, a correct constant and a
    /// correct predicate that no provider registers (#1286's exact shape).
    #[tokio::test]
    async fn the_latency_view_applies_the_microsecond_bounds_to_the_exported_histogram() {
        let rig = rig();
        assert_eq!(send(&rig, "GET", "/widgets/7").await, StatusCode::OK);

        let rm = exported(&rig);
        let mut found = false;
        for scope in rm.scope_metrics() {
            for metric in scope.metrics() {
                if metric.name() != "http_server_request_duration_microseconds" {
                    continue;
                }
                let AggregatedMetrics::F64(MetricData::Histogram(histogram)) = metric.data() else {
                    panic!("the duration histogram exported as something other than f64");
                };
                for dp in histogram.data_points() {
                    found = true;
                    assert_eq!(
                        dp.bounds().collect::<Vec<f64>>(),
                        HTTP_LATENCY_BUCKET_BOUNDS_MICROS.to_vec(),
                        "the exported layout is not the microsecond one — the view stopped \
                         applying, which is #1286 with a correct constant"
                    );
                    assert_eq!(
                        sorted_attrs(dp.attributes()),
                        route_attrs("GET", "/widgets/{id}"),
                        "the histogram carries the same route-labeled set as the counters"
                    );
                }
            }
        }
        assert!(found, "no duration data point was exported");
    }
}
