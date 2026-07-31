#ifndef DOMAINS_PLATFORM_LIBS_FUTILITY_OTEL_CAPTURING_METRICS_RECORDER_H
#define DOMAINS_PLATFORM_LIBS_FUTILITY_OTEL_CAPTURING_METRICS_RECORDER_H

// The test double MetricsRecorder's virtual record methods exist for
// (metrics.h): captures every metric a service records so tests can assert
// what is counted — and, just as important, what never appears in a name or
// label. Extends the production recorder rather than replacing it, so the
// real instrument paths still run underneath against the no-op global meter;
// a service that would crash or throw inside RecordCounter still does so
// under test.
//
// Lives here rather than beside one service's fixture because a counter
// distinguishing two failure modes is worthless to an operator unless
// something fails when the label is wrong, and that argument is not
// specific to any one domain.

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "domains/platform/libs/futility/otel/metrics.h"

namespace futility::otel {

class CapturingMetricsRecorder final : public MetricsRecorder {
 public:
  explicit CapturingMetricsRecorder(const std::string& service_name = "test")
      : MetricsRecorder(service_name) {}

  struct Entry {
    std::string name;
    double value;
    std::map<std::string, std::string> attributes;
  };

  void RecordCounter(const std::string& name, int64_t value,
                     const std::map<std::string, std::string>& attributes) override {
    Add(name, static_cast<double>(value), attributes);
    MetricsRecorder::RecordCounter(name, value, attributes);
  }
  void RecordLatency(const std::string& name, std::chrono::microseconds duration,
                     const std::map<std::string, std::string>& attributes) override {
    Add(name, static_cast<double>(duration.count()), attributes);
    MetricsRecorder::RecordLatency(name, duration, attributes);
  }
  void RecordDistribution(const std::string& name, double value,
                          const std::map<std::string, std::string>& attributes) override {
    Add(name, value, attributes);
    MetricsRecorder::RecordDistribution(name, value, attributes);
  }
  void RecordGauge(const std::string& name, double value,
                   const std::map<std::string, std::string>& attributes) override {
    Add(name, value, attributes);
    MetricsRecorder::RecordGauge(name, value, attributes);
  }

  /// Sum of increments recorded for the counter under exactly these
  /// attributes; 0 when it never fired. Exact-match on attributes is
  /// deliberate: asserting a labelled counter fired says nothing unless a
  /// differently-labelled one fails the same assertion.
  double CounterTotal(const std::string& name,
                      const std::map<std::string, std::string>& attributes = {}) const {
    const std::lock_guard<std::mutex> lock(mu_);
    double total = 0;
    for (const Entry& entry : entries_) {
      if (entry.name == name && entry.attributes == attributes) total += entry.value;
    }
    return total;
  }

  std::vector<Entry> Entries() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return entries_;
  }

 private:
  void Add(const std::string& name, double value,
           const std::map<std::string, std::string>& attributes) {
    const std::lock_guard<std::mutex> lock(mu_);
    entries_.push_back({name, value, attributes});
  }

  mutable std::mutex mu_;
  std::vector<Entry> entries_;
};

}  // namespace futility::otel

#endif
