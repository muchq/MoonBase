#pragma once

/// @file http_metrics.h
/// @brief The shared HTTP serving instruments (http_server_requests,
/// http_server_requests_active, http_server_request_duration,
/// http_server_requests_success / _failure) with service_name/route/method
/// labels, so services on any transport emit the same names and existing
/// dashboards keep working. Originally meerkat's metrics interceptor state;
/// rehomed here when portrait moved to smithy-cpp
/// (https://github.com/muchq/MoonBase/issues/1174).

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/otel/metrics.h"

namespace futility::otel {

class HttpMetricsManager {
 public:
  explicit HttpMetricsManager(const std::string& service_name);
  ~HttpMetricsManager() = default;

  // Called at request start
  void RecordRequestStart(const std::string& route, const std::string& method);

  // Called at request completion
  void RecordRequestComplete(const std::string& route, const std::string& method, int status_code,
                             std::chrono::microseconds duration);

 private:
  std::string service_name_;
  std::unique_ptr<MetricsRecorder> recorder_;

  // Helper methods
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
