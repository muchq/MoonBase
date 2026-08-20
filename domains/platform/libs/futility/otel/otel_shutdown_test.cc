#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "opentelemetry/exporters/memory/in_memory_metric_data.h"
#include "opentelemetry/exporters/memory/in_memory_metric_exporter_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

// What ~OtelProvider owes a short-lived process: everything recorded since the
// last interval, exported, and neither half of that waiting on a collector
// forever.

namespace futility::otel {
namespace {

namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace memory_exporter = opentelemetry::exporter::memory;

constexpr const char* kScope = "otel_shutdown_test";

// Accepts the handshake and never answers. A refused connection is not the
// failure this is about — the exporter gives up on that in microseconds. This
// is the collector that is up and wedged, which is the one that can hold an
// exit open.
class WedgedCollector {
 public:
  WedgedCollector() {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(fd_, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    EXPECT_EQ(::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    EXPECT_EQ(::listen(fd_, 16), 0);

    socklen_t len = sizeof(addr);
    EXPECT_EQ(::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len), 0);
    port_ = ::ntohs(addr.sin_port);
  }

  ~WedgedCollector() { ::close(fd_); }

  WedgedCollector(const WedgedCollector&) = delete;
  WedgedCollector& operator=(const WedgedCollector&) = delete;

  std::string Endpoint() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/v1/metrics";
  }

 private:
  int fd_ = -1;
  int port_ = 0;
};

TEST(OtelProviderShutdownTest, FlushesWhatItHasOnTheWayOut) {
  // The export interval is ten seconds. A worker that starts, does its work
  // and is stopped inside one of those never exports anything at all — which
  // is exactly the shape of a rollout, and of a service failing every run
  // and being turned off. The counts that would have told you what happened
  // die with the process.
  auto data = std::make_shared<memory_exporter::SimpleAggregateInMemoryMetricData>();
  std::shared_ptr<metrics_sdk::MeterProvider> published;

  {
    OtelConfig config;
    config.service_name = "otel_provider_shutdown_test";
    config.enable_metrics = true;
    config.otlp_endpoint = "http://127.0.0.1:1/v1/metrics";
    // Longer than this test could possibly take, so nothing is exported on a
    // timer and the only thing that can flush is the shutdown.
    config.export_interval = std::chrono::seconds(3600);

    OtelProvider provider(config);
    published = std::static_pointer_cast<metrics_sdk::MeterProvider>(provider.GetMeterProvider());
    ASSERT_NE(published, nullptr);

    metrics_sdk::PeriodicExportingMetricReaderOptions options;
    options.export_interval_millis = std::chrono::milliseconds(3600000);
    options.export_timeout_millis = std::chrono::milliseconds(1000);
    published->AddMetricReader(metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
        memory_exporter::InMemoryMetricExporterFactory::Create(data), options));

    auto meter = published->GetMeter(kScope, "1.0.0");
    auto counter = meter->CreateUInt64Counter("index_runs");
    counter->Add(1);

    ASSERT_TRUE(data->Get(kScope, "index_runs").empty())
        << "something exported on its own, so this test cannot tell a shutdown flush from a timer";
  }

  EXPECT_FALSE(data->Get(kScope, "index_runs").empty())
      << "the provider went away without flushing, so everything recorded since the last interval "
         "is gone — including every metric a short-lived process ever wrote";
}

// The flush is not the whole shutdown. Deleting the explicit Shutdown leaves
// the readers running, and the flush test above cannot see the difference —
// its only observable is what was exported, and a shutdown exports nothing
// either way.
TEST(OtelProviderShutdownTest, ShutsTheReadersDownAsWellAsFlushingThem) {
  auto data = std::make_shared<memory_exporter::SimpleAggregateInMemoryMetricData>();
  std::shared_ptr<metrics_sdk::MeterProvider> published;

  {
    OtelConfig config;
    config.service_name = "otel_provider_shutdown_test";
    config.enable_metrics = true;
    config.otlp_endpoint = "http://127.0.0.1:1/v1/metrics";
    config.export_interval = std::chrono::seconds(3600);

    OtelProvider provider(config);
    published = std::static_pointer_cast<metrics_sdk::MeterProvider>(provider.GetMeterProvider());
    ASSERT_NE(published, nullptr);

    metrics_sdk::PeriodicExportingMetricReaderOptions options;
    options.export_interval_millis = std::chrono::milliseconds(3600000);
    options.export_timeout_millis = std::chrono::milliseconds(1000);
    published->AddMetricReader(metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
        memory_exporter::InMemoryMetricExporterFactory::Create(data), options));
  }

  // Held past the scope, so nothing but ~OtelProvider can have shut this down.
  auto meter = published->GetMeter(kScope, "1.0.0");
  auto counter = meter->CreateUInt64Counter("after_shutdown");
  counter->Add(1);
  published->ForceFlush();

  EXPECT_TRUE(data->Get(kScope, "after_shutdown").empty())
      << "the readers are still collecting after the provider was destroyed";
}

// The bound, not just the call. ForceFlush maps a zero timeout to an
// indefinite wait, so dropping the bound turns one stalled export into two.
//
// The numbers are not free-floating: opentelemetry-cpp hardcodes
// CURLOPT_LOW_SPEED_TIME at 30s, so a wedged collector costs ~30s bounded and
// ~60s unbounded, whatever kShutdownTimeout is. 45s is the midpoint. This test
// therefore spends half a minute on purpose, which is why it has its own
// target.
TEST(OtelProviderShutdownTest, AWedgedCollectorDoesNotHoldUpTheExit) {
  WedgedCollector collector;

  // Far past both bounds, so the configured export timeout cannot be what
  // ends the wait — leaving curl's hardcoded low-speed abort as the only
  // ceiling, which is the point.
  ::setenv("OTEL_EXPORTER_OTLP_METRICS_TIMEOUT", "120s", 1);

  const auto started = std::chrono::steady_clock::now();
  {
    OtelConfig config;
    config.service_name = "otel_provider_shutdown_test";
    config.enable_metrics = true;
    config.otlp_endpoint = collector.Endpoint();
    config.export_interval = std::chrono::seconds(3600);

    OtelProvider provider(config);
    auto meter = provider.GetMeterProvider()->GetMeter(kScope, "1.0.0");
    auto counter = meter->CreateUInt64Counter("index_runs");
    counter->Add(1);
  }
  const auto elapsed = std::chrono::steady_clock::now() - started;

  ::unsetenv("OTEL_EXPORTER_OTLP_METRICS_TIMEOUT");

  EXPECT_LT(elapsed, std::chrono::seconds(45))
      << "the exit waited " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
      << "s on a collector that never answers";
}

}  // namespace
}  // namespace futility::otel
