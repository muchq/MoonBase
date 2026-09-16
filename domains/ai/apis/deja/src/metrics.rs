use opentelemetry::metrics::{Counter, Gauge, Meter};
use opentelemetry::{KeyValue, global};

use crate::{engine::Event, score::Verdict};

/// The predictors whose surprise is summed and whose baseline is gauged.
const PREDICTORS: [&str; 2] = ["bigram", "net"];

pub struct AppMetrics {
    pub events_total: Counter<u64>,
    /// A sum of surprise rather than a histogram, for the same reason as
    /// microgpt's inference_ms: mean surprise is rate(surprise)/rate(events),
    /// and a counter can be declared at zero without biasing that.
    pub surprise_total: Counter<f64>,
    /// Each predictor's own EWMA of surprise, the baseline an event is
    /// judged against; the learning curve, as the page plots it.
    pub ewma_loss: Gauge<f64>,
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
            ewma_loss: meter
                .f64_gauge("deja_ewma_loss")
                .with_description("EWMA of surprise by predictor")
                .build(),
            vocab_size: meter
                .u64_gauge("deja_vocab_size")
                .with_description("Distinct request tokens seen")
                .build(),
        };
        metrics.declare();
        metrics
    }

    /// Every series at zero before the first request lands in it (#1323):
    /// the SDK exports nothing for an instrument with no measurement, and a
    /// series born on its first event has no earlier sample for
    /// `increase()` to measure against.
    fn declare(&self) {
        for verdict in Verdict::ALL {
            self.events_total
                .add(0, &[KeyValue::new("verdict", verdict.as_str())]);
        }
        for predictor in PREDICTORS {
            self.surprise_total
                .add(0.0, &[KeyValue::new("predictor", predictor)]);
            self.ewma_loss
                .record(0.0, &[KeyValue::new("predictor", predictor)]);
        }
        self.vocab_size.record(0, &[]);
    }

    pub fn record(&self, event: &Event) {
        self.events_total
            .add(1, &[KeyValue::new("verdict", event.verdict.as_str())]);
        for (predictor, surprise, ewma_loss) in [
            ("bigram", event.surprise.bigram, event.ewma_loss.bigram),
            ("net", event.surprise.net, event.ewma_loss.net),
        ] {
            let labels = [KeyValue::new("predictor", predictor)];
            self.surprise_total.add(surprise, &labels);
            self.ewma_loss.record(ewma_loss, &labels);
        }
        self.vocab_size.record(event.vocab_size as u64, &[]);
    }
}

#[cfg(test)]
mod tests {
    use opentelemetry::metrics::MeterProvider as _;
    use opentelemetry_sdk::metrics::data::{AggregatedMetrics, MetricData, ResourceMetrics};
    use opentelemetry_sdk::metrics::{InMemoryMetricExporter, PeriodicReader, SdkMeterProvider};

    use super::*;
    use crate::engine::{Engine, fixtures::*};

    struct Rig {
        provider: SdkMeterProvider,
        exporter: InMemoryMetricExporter,
        metrics: AppMetrics,
    }

    /// An isolated provider, so this reads only what AppMetrics recorded.
    fn rig() -> Rig {
        let exporter = InMemoryMetricExporter::default();
        let provider = SdkMeterProvider::builder()
            .with_reader(PeriodicReader::builder(exporter.clone()).build())
            .build();
        let metrics = AppMetrics::with_meter(&provider.meter("deja"));
        Rig {
            provider,
            exporter,
            metrics,
        }
    }

    /// `name{k="v"}` and its value for every series the export carries —
    /// sums and gauges alike — so an undeclared series fails, not just a
    /// missing one.
    fn series(rig: &Rig) -> Vec<(String, f64)> {
        rig.provider.force_flush().expect("force_flush failed");
        let rm: ResourceMetrics = rig
            .exporter
            .get_finished_metrics()
            .expect("get_finished_metrics failed")
            .pop()
            .expect("nothing was exported");
        fn label<'a>(name: &str, attrs: impl Iterator<Item = &'a KeyValue>) -> String {
            let mut pairs: Vec<String> = attrs
                .map(|kv| format!("{}=\"{}\"", kv.key, kv.value))
                .collect();
            if pairs.is_empty() {
                return name.to_string();
            }
            pairs.sort();
            format!("{}{{{}}}", name, pairs.join(","))
        }
        let mut out = vec![];
        for scope in rm.scope_metrics() {
            for metric in scope.metrics() {
                match metric.data() {
                    AggregatedMetrics::U64(MetricData::Sum(sum)) => {
                        for dp in sum.data_points() {
                            out.push((label(metric.name(), dp.attributes()), dp.value() as f64));
                        }
                    }
                    AggregatedMetrics::U64(MetricData::Gauge(gauge)) => {
                        for dp in gauge.data_points() {
                            out.push((label(metric.name(), dp.attributes()), dp.value() as f64));
                        }
                    }
                    AggregatedMetrics::F64(MetricData::Sum(sum)) => {
                        for dp in sum.data_points() {
                            out.push((label(metric.name(), dp.attributes()), dp.value()));
                        }
                    }
                    AggregatedMetrics::F64(MetricData::Gauge(gauge)) => {
                        for dp in gauge.data_points() {
                            out.push((label(metric.name(), dp.attributes()), dp.value()));
                        }
                    }
                    other => panic!("{}: unexpected aggregation {other:?}", metric.name()),
                }
            }
        }
        out.sort_by(|a, b| a.0.cmp(&b.0));
        out
    }

    /// Every series, `surprise` and `ewma_loss` as `(bigram, net)`.
    fn expected(
        verdicts: [f64; 4],
        surprise: (f64, f64),
        ewma_loss: (f64, f64),
        vocab: f64,
    ) -> Vec<(String, f64)> {
        let [warmup, expected, anomaly, novel] = verdicts;
        let mut out = vec![
            (r#"deja_events{verdict="anomaly"}"#.to_string(), anomaly),
            (r#"deja_events{verdict="expected"}"#.to_string(), expected),
            (r#"deja_events{verdict="novel"}"#.to_string(), novel),
            (r#"deja_events{verdict="warmup"}"#.to_string(), warmup),
            (
                r#"deja_surprise{predictor="bigram"}"#.to_string(),
                surprise.0,
            ),
            (r#"deja_surprise{predictor="net"}"#.to_string(), surprise.1),
            (
                r#"deja_ewma_loss{predictor="bigram"}"#.to_string(),
                ewma_loss.0,
            ),
            (
                r#"deja_ewma_loss{predictor="net"}"#.to_string(),
                ewma_loss.1,
            ),
            ("deja_vocab_size".to_string(), vocab),
        ];
        out.sort_by(|a, b| a.0.cmp(&b.0));
        out
    }

    #[test]
    fn building_the_metrics_declares_every_series_at_zero() {
        let rig = rig();
        assert_eq!(
            series(&rig),
            expected([0.0; 4], (0.0, 0.0), (0.0, 0.0), 0.0)
        );
    }

    #[test]
    fn recording_lands_on_the_declared_series_and_nothing_else() {
        let rig = rig();
        let mut engine = Engine::default();
        let novel = engine.ingest(&request(1.0, "1.1.1.1", "GET", "/", 200, BROWSER));
        let warmup = engine.ingest(&request(2.0, "1.1.1.1", "GET", "/", 200, BROWSER));
        assert_eq!(novel.verdict, Verdict::Novel);
        assert_eq!(warmup.verdict, Verdict::Warmup);
        rig.metrics.record(&novel);
        rig.metrics.record(&warmup);
        let judged = engine.ingest(&request(3.0, "1.1.1.1", "GET", "/", 200, BROWSER));
        rig.metrics.record(&judged);
        assert!(
            judged.ewma_loss.net > 0.0,
            "the gauge reads the net's baseline"
        );
        assert_eq!(
            series(&rig),
            expected(
                [2.0, 0.0, 0.0, 1.0],
                (
                    novel.surprise.bigram + warmup.surprise.bigram + judged.surprise.bigram,
                    novel.surprise.net + warmup.surprise.net + judged.surprise.net
                ),
                (judged.ewma_loss.bigram, judged.ewma_loss.net),
                3.0
            )
        );
    }

    /// No instrument declares a unit (#1294): the collector's Prometheus
    /// exporter folds a unit into the metric name, and the dashboard selects
    /// by literal name.
    #[test]
    fn no_instrument_declares_a_unit() {
        let rig = rig();
        rig.provider.force_flush().unwrap();
        let rm = rig.exporter.get_finished_metrics().unwrap().pop().unwrap();
        let mut swept = 0;
        for scope in rm.scope_metrics() {
            for metric in scope.metrics() {
                swept += 1;
                assert_eq!(metric.unit(), "", "{} declares a unit", metric.name());
            }
        }
        assert_eq!(swept, 4);
    }
}
