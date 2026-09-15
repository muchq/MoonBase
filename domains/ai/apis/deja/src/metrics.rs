use opentelemetry::metrics::{Counter, Gauge, Meter};
use opentelemetry::{KeyValue, global};

/// Every verdict an event can carry, so each series is declared at zero
/// (#1323) before the first request lands in it.
const VERDICTS: [&str; 4] = ["warmup", "expected", "anomaly", "novel"];
/// The predictors whose surprise is summed; `net` joins in Phase 3.
const PREDICTORS: [&str; 1] = ["bigram"];

pub struct AppMetrics {
    pub events_total: Counter<u64>,
    /// A sum of surprise rather than a histogram, for the same reason as
    /// microgpt's inference_ms: mean surprise is rate(surprise)/rate(events),
    /// and a counter can be declared at zero without biasing that.
    pub surprise_total: Counter<f64>,
    pub vocab_size: Gauge<u64>,
}

impl AppMetrics {
    pub fn new() -> Self {
        Self::with_meter(&global::meter("deja"))
    }

    pub fn with_meter(meter: &Meter) -> Self {
        let metrics = AppMetrics {
            events_total: meter
                .u64_counter("deja_events")
                .with_description("Requests scored, by verdict")
                .build(),
            surprise_total: meter
                .f64_counter("deja_surprise")
                .with_description("Cumulative surprise (-ln p) by predictor")
                .build(),
            vocab_size: meter
                .u64_gauge("deja_vocab_size")
                .with_description("Distinct request tokens seen")
                .build(),
        };
        metrics.declare();
        metrics
    }

    fn declare(&self) {
        for verdict in VERDICTS {
            self.events_total
                .add(0, &[KeyValue::new("verdict", verdict)]);
        }
        for predictor in PREDICTORS {
            self.surprise_total
                .add(0.0, &[KeyValue::new("predictor", predictor)]);
        }
        self.vocab_size.record(0, &[]);
    }

    pub fn record(&self, event: &crate::engine::Event) {
        self.events_total
            .add(1, &[KeyValue::new("verdict", event.verdict)]);
        self.surprise_total.add(
            event.surprise.bigram,
            &[KeyValue::new("predictor", "bigram")],
        );
        self.vocab_size.record(event.vocab_size as u64, &[]);
    }
}
