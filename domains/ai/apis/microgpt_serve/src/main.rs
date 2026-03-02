mod metrics;
mod service;
mod types;

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process;
use std::sync::Arc;
use std::time::Duration;

use axum::routing::post;
use microgpt::model::ModelMeta;
use microgpt::{InferenceGpt, Tokenizer};
use opentelemetry_otlp::MetricExporter;
use opentelemetry_sdk::Resource;
use opentelemetry_sdk::metrics::SdkMeterProvider;
use server_pal::{RateLimit, listen_addr_pal, router_builder, serve};
use tracing::{Level, event};

use crate::metrics::AppMetrics;
use crate::service::{chat_post, generate_post};

pub struct AppState {
    pub model: InferenceGpt,
    pub tokenizer: Tokenizer,
    pub metrics: AppMetrics,
}

/// Initialise the global OTel meter provider if OTEL_EXPORTER_OTLP_ENDPOINT is
/// set.  Returns the provider so the caller can keep it alive for the lifetime
/// of the process (dropping it shuts down the exporter).
fn init_otel() -> Option<SdkMeterProvider> {
    let endpoint = env::var("OTEL_EXPORTER_OTLP_ENDPOINT").ok()?;

    let exporter = MetricExporter::builder()
        .with_http()
        .with_endpoint(format!("{}/v1/metrics", endpoint))
        .with_timeout(Duration::from_secs(5))
        .build()
        .unwrap_or_else(|e| {
            eprintln!("warning: failed to create OTLP metric exporter: {e}");
            process::exit(1);
        });

    // Resource::builder() automatically picks up OTEL_SERVICE_NAME and
    // OTEL_RESOURCE_ATTRIBUTES via the built-in EnvResourceDetector.
    let resource = Resource::builder().build();

    let provider = SdkMeterProvider::builder()
        .with_periodic_exporter(exporter)
        .with_resource(resource)
        .build();

    opentelemetry::global::set_meter_provider(provider.clone());
    event!(Level::INFO, "OTel metrics initialised (endpoint: {})", endpoint);
    Some(provider)
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    // Must be initialised before AppMetrics::new() so the global provider is
    // in place when OTel instruments are created.
    let _otel_provider = init_otel();

    let model_dir = env::var("MODEL_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("output"));

    let meta_path = model_dir.join("meta.json");

    let meta_json = fs::read_to_string(&meta_path).unwrap_or_else(|e| {
        eprintln!(
            "error: cannot read {}: {e}\nSet MODEL_DIR to the directory containing weights.safetensors and meta.json",
            meta_path.display()
        );
        process::exit(1);
    });
    let meta: ModelMeta = serde_json::from_str(&meta_json).unwrap_or_else(|e| {
        eprintln!("error: invalid meta.json: {e}");
        process::exit(1);
    });

    let config = meta.config();
    let model = load_model(&model_dir, &meta, config);

    let tok_path = model_dir.join("tokenizer.json");
    let tokenizer = Tokenizer::from_file(&tok_path).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    event!(
        Level::INFO,
        "loaded model: vocab_size={}, params={}",
        tokenizer.vocab_size,
        model.num_params()
    );

    let metrics = AppMetrics::new();
    let state = Arc::new(AppState { model, tokenizer, metrics });

    let listen_address = listen_addr_pal();
    let has_chat = state.tokenizer.special_tokens.is_some();
    event!(Level::INFO, "chat support: {}", has_chat);

    let app = router_builder()
        .route("/microgpt/v1/generate", post(generate_post))
        .route("/microgpt/v1/chat", post(chat_post))
        .rate_limit(Some(RateLimit { per_second: 5, burst: 10 }))
        .build()
        .with_state(state);

    event!(Level::INFO, "listening on {}", listen_address);
    serve(app, &listen_address).await;
}

/// Load model weights from safetensors.
fn load_model(
    model_dir: &Path,
    meta: &ModelMeta,
    config: microgpt::ModelConfig,
) -> InferenceGpt {
    let st_path = model_dir.join("weights.safetensors");
    let bytes = fs::read(&st_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", st_path.display());
        process::exit(1);
    });
    InferenceGpt::load_safetensors(meta.vocab_size, &bytes, config)
        .unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        })
}
