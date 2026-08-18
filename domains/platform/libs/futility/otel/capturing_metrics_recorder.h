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

  // Captured separately from the record methods, and deliberately not as an
  // entry: a declaration adds zero, so a test that could not tell the two
  // apart would read "declared" and "fired with value 0" as the same event
  // — which is the whole distinction #1323 turns on.
  // The default is repeated, not inherited: a virtual's default arguments
  // are not, and an override without one hides the base's single-argument
  // form from every caller holding this type.
  void DeclareCounter(const std::string& name,
                      const std::map<std::string, std::string>& attributes = {}) override {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      declared_.push_back({name, 0, attributes});
    }
    MetricsRecorder::DeclareCounter(name, attributes);
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

  /// How many observations landed under exactly these attributes.
  ///
  /// The count, not the sum: a histogram of durations says nothing useful
  /// about how many times it fired if the assertion adds the values up.
  int ObservationCount(const std::string& name,
                       const std::map<std::string, std::string>& attributes = {}) const {
    const std::lock_guard<std::mutex> lock(mu_);
    int count = 0;
    for (const Entry& entry : entries_) {
      if (entry.name == name && entry.attributes == attributes) ++count;
    }
    return count;
  }

  /// True when this exact series was declared ahead of its first event.
  bool Declared(const std::string& name,
                const std::map<std::string, std::string>& attributes = {}) const {
    const std::lock_guard<std::mutex> lock(mu_);
    for (const Entry& entry : declared_) {
      if (entry.name == name && entry.attributes == attributes) return true;
    }
    return false;
  }

  std::vector<Entry> Entries() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return entries_;
  }

  std::vector<Entry> Declarations() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return declared_;
  }

 private:
  void Add(const std::string& name, double value,
           const std::map<std::string, std::string>& attributes) {
    const std::lock_guard<std::mutex> lock(mu_);
    entries_.push_back({name, value, attributes});
  }

  mutable std::mutex mu_;
  std::vector<Entry> entries_;
  std::vector<Entry> declared_;
};

}  // namespace futility::otel

#endif
