#include "domains/platform/libs/meerkat/metrics_manager.h"

#include <string>

namespace meerkat {

HttpMetricsManager::HttpMetricsManager(const std::string& service_name)
    : service_name_(service_name),
      recorder_(std::make_unique<futility::otel::MetricsRecorder>(service_name)) {
}

void HttpMetricsManager::RecordRequestStart(const std::string& route, const std::string& method) {
  if (!recorder_) return;

  auto base_attrs = CreateBaseAttributes(route, method);

  // Increment total requests counter
  recorder_->RecordCounter("http_server_requests", 1, base_attrs);

  // Increment active requests gauge
  recorder_->RecordGauge("http_server_requests_active", 1, base_attrs);
}

void HttpMetricsManager::RecordRequestComplete(const std::string& route,
                                             const std::string& method,
                                             int status_code,
                                             std::chrono::microseconds duration) {
  if (!recorder_) return;

  auto base_attrs = CreateBaseAttributes(route, method);
  auto request_attrs = CreateRequestAttributes(route, method, status_code);

  // Decrement active requests gauge
  recorder_->RecordGauge("http_server_requests_active", -1, base_attrs);

  // Record request duration
  recorder_->RecordLatency("http_server_request_duration", duration, request_attrs);

  // Record success or failure
  if (IsSuccess(status_code)) {
    recorder_->RecordCounter("http_server_requests_success", 1, base_attrs);
  } else {
    auto failure_attrs = request_attrs;
    failure_attrs["error_type"] = DetermineErrorType(status_code);
    recorder_->RecordCounter("http_server_requests_failure", 1, failure_attrs);
  }
}

std::map<std::string, std::string> HttpMetricsManager::CreateBaseAttributes(
    const std::string& route, const std::string& method) const {
  return {
    {"service_name", service_name_},
    {"route", route},
    {"method", method}
  };
}

std::map<std::string, std::string> HttpMetricsManager::CreateRequestAttributes(
    const std::string& route, const std::string& method, int status_code) const {
  auto attrs = CreateBaseAttributes(route, method);
  attrs["status_code"] = std::to_string(status_code);
  attrs["result"] = DetermineResult(status_code);
  return attrs;
}

std::string HttpMetricsManager::DetermineErrorType(int status_code) const {
  if (status_code == 429) return "rate_limited";
  if (status_code >= 400 && status_code < 500) return "client_error";
  if (status_code >= 500) return "server_error";
  return "unknown";
}

std::string HttpMetricsManager::DetermineResult(int status_code) const {
  return IsSuccess(status_code) ? "success" : "failure";
}

bool HttpMetricsManager::IsSuccess(int status_code) const {
  return status_code >= 200 && status_code < 400;
}

}  // namespace meerkat