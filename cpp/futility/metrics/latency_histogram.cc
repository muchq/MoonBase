#include "cpp/futility/metrics/latency_histogram.h"

#include <stdexcept>
#include <algorithm>

namespace futility::metrics {

LatencyHistogram::LatencyHistogram(int64_t min_value, int64_t max_value, int significant_figures)
    : histogram_(nullptr) {
  
  if (min_value <= 0) {
    throw std::invalid_argument("min_value must be positive");
  }
  if (max_value <= min_value) {
    throw std::invalid_argument("max_value must be greater than min_value");
  }
  if (significant_figures < 1 || significant_figures > 5) {
    throw std::invalid_argument("significant_figures must be between 1 and 5");
  }
  
  int result = hdr_init(min_value, max_value, significant_figures, &histogram_);
  if (result != 0) {
    throw std::runtime_error("Failed to initialize HDR histogram");
  }
}

LatencyHistogram::~LatencyHistogram() {
  if (histogram_) {
    hdr_close(histogram_);
  }
}

LatencyHistogram::LatencyHistogram(LatencyHistogram&& other) noexcept
    : histogram_(other.histogram_) {
  other.histogram_ = nullptr;
}

LatencyHistogram& LatencyHistogram::operator=(LatencyHistogram&& other) noexcept {
  if (this != &other) {
    if (histogram_) {
      hdr_close(histogram_);
    }
    histogram_ = other.histogram_;
    other.histogram_ = nullptr;
  }
  return *this;
}

void LatencyHistogram::record(int64_t value_microseconds) {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  ensure_initialized();
  hdr_record_value(histogram_, value_microseconds);
}

void LatencyHistogram::record_multiple(int64_t value_microseconds, int64_t count) {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  ensure_initialized();
  hdr_record_values(histogram_, value_microseconds, count);
}

double LatencyHistogram::get_percentile(double percentile) const {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  if (!histogram_) return 0.0;
  return static_cast<double>(hdr_value_at_percentile(histogram_, percentile));
}

int64_t LatencyHistogram::get_value_at_percentile(double percentile) const {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  if (!histogram_) return 0;
  return hdr_value_at_percentile(histogram_, percentile);
}

int64_t LatencyHistogram::get_min() const {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  if (!histogram_ || histogram_->total_count == 0) return 0;
  return hdr_min(histogram_);
}

int64_t LatencyHistogram::get_max() const {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  if (!histogram_ || histogram_->total_count == 0) return 0;
  return hdr_max(histogram_);
}

double LatencyHistogram::get_mean() const {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  if (!histogram_ || histogram_->total_count == 0) return 0.0;
  return hdr_mean(histogram_);
}

int64_t LatencyHistogram::get_total_count() const {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  if (!histogram_) return 0;
  return histogram_->total_count;
}

void LatencyHistogram::reset() {
  std::lock_guard<std::mutex> lock(histogram_mutex_);
  if (histogram_) {
    hdr_reset(histogram_);
  }
}

void LatencyHistogram::ensure_initialized() {
  if (!histogram_) {
    throw std::runtime_error("Histogram not properly initialized");
  }
}

}  // namespace futility::metrics