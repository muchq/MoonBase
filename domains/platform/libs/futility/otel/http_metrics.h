#pragma once

/// @file http_metrics.h
/// @brief The shared HTTP serving instruments (http_server_requests,
/// http_server_requests_active, http_server_request_duration,
/// http_server_requests_success / _failure) with service_name / route /
/// http_method labels, so services on any transport emit the same names and
/// existing dashboards keep working
/// (https://github.com/muchq/MoonBase/issues/1174).
///
/// The route is a bounded value — the matched handler (aura passes the Smithy
/// operation name), the "/health" literal, or a fixed sentinel — never the raw
/// request path, which minted a Prometheus series per distinct path a scanner
/// tried (https://github.com/muchq/MoonBase/issues/1305). The label set and
/// the recording contract mirror yodel's HttpServerMetrics: the counters and
/// the histogram move at completion, where the route and status are known;
/// only the in-flight gauge moves at request start, keyed by http_method and
/// service_name alone. //domains/platform/libs/otel_contract pins the label
/// sets across the three rails.

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/otel/metrics.h"

namespace futility::otel {

class HttpMetricsManager {
 public:
  explicit HttpMetricsManager(const std::string& service_name);

  /// The recorder-injecting constructor tests use to observe what is
  /// recorded (names, values, and exactly which labels — including the ones
  /// that must never appear). Production always uses the one-argument form.
  HttpMetricsManager(const std::string& service_name, std::unique_ptr<MetricsRecorder> recorder);

  ~HttpMetricsManager() = default;

  /// Called at request start. Routing hasn't happened yet, so only the
  /// in-flight gauge can move here, and it carries no route.
  void RecordRequestStart(const std::string& method);

  /// Called at request completion: drains the gauge and records the request
  /// counter, the outcome counter, and the duration histogram, all labeled
  /// with the bounded route.
  void RecordRequestComplete(const std::string& route, const std::string& method, int status_code,
                             std::chrono::microseconds duration);

 private:
  std::string service_name_;
  std::unique_ptr<MetricsRecorder> recorder_;

  // Helper methods
  std::map<std::string, std::string> CreateGaugeAttributes(const std::string& method) const;

  std::map<std::string, std::string> CreateBaseAttributes(const std::string& route,
                                                          const std::string& method) const;

  std::map<std::string, std::string> CreateRequestAttributes(const std::string& route,
                                                             const std::string& method,
                                                             int status_code) const;

  std::string DetermineErrorType(int status_code) const;
  std::string DetermineResult(int status_code) const;
  bool IsSuccess(int status_code) const;
};

}  // namespace futility::otel
