#ifndef DOMAINS_API_PLATFORM_LIBS_MEERKAT_METRICS_MANAGER_H
#define DOMAINS_API_PLATFORM_LIBS_MEERKAT_METRICS_MANAGER_H

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/otel/metrics.h"

namespace meerkat {

class HttpMetricsManager {
 public:
  explicit HttpMetricsManager(const std::string& service_name);
  ~HttpMetricsManager() = default;

  // Called at request start
  void RecordRequestStart(const std::string& route, const std::string& method);

  // Called at request completion
  void RecordRequestComplete(const std::string& route,
                           const std::string& method,
                           int status_code,
                           std::chrono::microseconds duration);

 private:
  std::string service_name_;
  std::unique_ptr<futility::otel::MetricsRecorder> recorder_;

  // Helper methods
  std::map<std::string, std::string> CreateBaseAttributes(
      const std::string& route, const std::string& method) const;

  std::map<std::string, std::string> CreateRequestAttributes(
      const std::string& route, const std::string& method, int status_code) const;

  std::string DetermineErrorType(int status_code) const;
  std::string DetermineResult(int status_code) const;
  bool IsSuccess(int status_code) const;
};

}  // namespace meerkat

#endif  // DOMAINS_API_PLATFORM_LIBS_MEERKAT_METRICS_MANAGER_H