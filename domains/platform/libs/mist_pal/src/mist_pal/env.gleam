//// Process environment and clocks.

@external(erlang, "mist_pal_ffi", "getenv")
pub fn getenv(name: String) -> Result(String, Nil)

/// Microseconds on a clock that never goes backwards, for durations.
@external(erlang, "mist_pal_ffi", "monotonic_micros")
pub fn monotonic_micros() -> Int

/// Wall-clock nanoseconds since the Unix epoch, for timestamps.
@external(erlang, "mist_pal_ffi", "system_time_nanos")
pub fn system_time_nanos() -> Int
