//// The few Erlang runtime facilities the service needs that the Gleam
//// packages it uses do not wrap.

/// A fixed-size, zero-indexed sequence with constant-time reads: an Erlang
/// tuple.
pub type Array(a)

@external(erlang, "mithril_v2_ffi", "array_from_list")
pub fn array_from_list(items: List(a)) -> Array(a)

/// Crashes on an index out of range.
@external(erlang, "mithril_v2_ffi", "array_get")
pub fn array_get(array: Array(a), index: Int) -> a

@external(erlang, "mithril_v2_ffi", "read_text")
pub fn read_text(path: String) -> Result(String, String)

@external(erlang, "mithril_v2_ffi", "read_bits")
pub fn read_bits(path: String) -> Result(BitArray, String)

@external(erlang, "mithril_v2_ffi", "getenv")
pub fn getenv(name: String) -> Result(String, Nil)

/// The main workspace's runfiles directory, which the release launcher and
/// gleam_test both export.
pub fn runfiles_dir() -> String {
  case getenv("GLEAM_RUNFILES_DIR") {
    Ok(dir) -> dir
    Error(Nil) -> "."
  }
}
