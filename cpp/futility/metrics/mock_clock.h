#ifndef CPP_FUTILITY_METRICS_MOCK_CLOCK_H
#define CPP_FUTILITY_METRICS_MOCK_CLOCK_H

#include <chrono>

namespace futility::metrics {

class MockClock {
 public:
  using duration = std::chrono::steady_clock::duration;
  using time_point = std::chrono::steady_clock::time_point;
  static constexpr bool is_steady = true;

  static time_point now() { 
    return current_time_; 
  }
  
  static void set_time(time_point t) { 
    current_time_ = t; 
  }
  
  static void advance_time(duration d) { 
    current_time_ += d; 
  }
  
  static void reset() {
    current_time_ = std::chrono::steady_clock::now();
  }

 private:
  static time_point current_time_;
};

// Static member definition
inline MockClock::time_point MockClock::current_time_ = std::chrono::steady_clock::now();

}  // namespace futility::metrics

#endif