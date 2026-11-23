//// Client-address derivation behind a reverse proxy: opal-cpp ADR-0012's
//// rightmost-untrusted walk, so the Gleam rail keys on the same client as
//// server_pal (Rust) and aura (C++).

import gleam/bit_array
import gleam/int
import gleam/list
import gleam/option.{type Option, None, Some}
import gleam/result
import gleam/string
import mist
import mist_pal/env

/// An address as its integer value: 32 bits for V4, 128 for V6. An
/// IPv4-mapped IPv6 address is always held as the V4 it embeds.
pub type Address {
  V4(Int)
  V6(Int)
}

/// The deployment's reverse-proxy trust boundary: the networks whose
/// x-forwarded-for entries count. Behind a proxy the accidental empty set
/// collapses every caller onto the proxy's one key, so "trust nothing" is
/// spelled `none()`.
pub opaque type TrustedProxies {
  TrustedProxies(networks: List(#(Address, Int)))
}

/// Which path produced a derived client, spelled as the C++ access log
/// spells it.
pub type ClientSource {
  /// Untrusted peer, no x-forwarded-for: the client itself.
  DirectPeer
  /// Untrusted peer; the header was present and ignored.
  UntrustedHeaderIgnored
  /// The trusted walk ended on the client's entry.
  Forwarded
  /// The walk never left the trust set (no header, chain exhausted, or
  /// stopped by a malformed entry).
  TrustedTier
  /// No peer at all; there is no address.
  Unknown
}

pub type DerivedClient {
  DerivedClient(address: Option(Address), source: ClientSource)
}

pub fn none() -> TrustedProxies {
  TrustedProxies([])
}

/// CIDR and bare-address (host route) entries, or an error naming the first
/// malformed one: a trust boundary that does not parse must fail startup,
/// not silently widen or narrow.
pub fn parse(cidrs: List(String)) -> Result(TrustedProxies, String) {
  list.try_map(cidrs, parse_network) |> result.map(TrustedProxies)
}

/// TRUSTED_PROXY_CIDRS, read as aura reads it.
pub fn from_env() -> Result(TrustedProxies, String) {
  from_env_value(env.getenv("TRUSTED_PROXY_CIDRS"))
}

/// Unset is the deliberate direct-connect statement; set but empty is
/// refused, since it can only be a config typo behind a proxy.
pub fn from_env_value(
  value: Result(String, Nil),
) -> Result(TrustedProxies, String) {
  case value {
    Error(Nil) -> Ok(none())
    Ok(value) ->
      case
        string.split(value, ",")
        |> list.map(trim_white_space)
        |> list.filter(fn(entry) { entry != "" })
      {
        [] ->
          Error(
            "TRUSTED_PROXY_CIDRS is set but empty; unset it to serve direct-connect",
          )
        cidrs ->
          parse(cidrs)
          |> result.map_error(fn(e) { "invalid TRUSTED_PROXY_CIDRS: " <> e })
      }
  }
}

/// IPv4-mapped IPv6 matches as the embedded IPv4.
pub fn contains(trusted: TrustedProxies, address: Address) -> Bool {
  let address = canonical(address)
  list.any(trusted.networks, fn(network) {
    let #(base, prefix) = network
    case base, address {
      V4(base), V4(address) -> same_prefix(base, address, 32, prefix)
      V6(base), V6(address) -> same_prefix(base, address, 128, prefix)
      _, _ -> False
    }
  })
}

fn same_prefix(base: Int, address: Int, bits: Int, prefix: Int) -> Bool {
  let shift = bits - prefix
  int.bitwise_shift_right(base, shift)
  == int.bitwise_shift_right(address, shift)
}

fn parse_network(cidr: String) -> Result(#(Address, Int), String) {
  let malformed = fn(what) {
    Error("TrustedProxies: " <> what <> " in " <> string.inspect(cidr))
  }
  let #(base_text, prefix_text) = case string.split_once(cidr, "/") {
    Ok(#(base, prefix)) -> #(base, Some(prefix))
    Error(Nil) -> #(cidr, None)
  }
  case parse_raw(base_text) {
    Error(Nil) -> malformed("unparseable address")
    Ok(#(base, mapped)) -> {
      let bits = case base {
        V4(_) -> 32
        V6(_) -> 128
      }
      case prefix_text {
        None -> Ok(#(base, bits))
        Some(text) -> {
          let digits =
            string.byte_size(text) >= 1 && string.byte_size(text) <= 3
          case digits && all_digits(text), int.parse(text) {
            True, Ok(written) -> {
              // A base written as IPv4-mapped IPv6 is the embedded IPv4, so
              // its prefix moves across the /96 mapping range with it.
              let prefix = case mapped {
                True -> written - 96
                False -> written
              }
              case prefix >= 0 && prefix <= bits && written <= 128 {
                True -> Ok(#(base, prefix))
                False -> malformed("prefix out of range")
              }
            }
            _, _ -> malformed("malformed prefix")
          }
        }
      }
    }
  }
}

/// A bare address, canonical: an IPv4-mapped IPv6 address is the V4 it
/// embeds. No port, brackets or zone.
pub fn parse_address(text: String) -> Result(Address, Nil) {
  parse_raw(text) |> result.map(fn(parsed) { parsed.0 })
}

// The address, and whether it was written as IPv4-mapped IPv6.
fn parse_raw(text: String) -> Result(#(Address, Bool), Nil) {
  // inet reads a v6 zone ("%eth0") as part of the address and an octet
  // with a sign, neither of which Rust's parser, and so server_pal, takes.
  use <- guard(
    string.contains(text, "%")
    || string.contains(text, "+")
    || string.contains(text, "-"),
  )
  use parsed <- result.map(ffi_parse_address(text))
  case parsed {
    #(Family4, value) -> #(V4(value), False)
    #(Family6, value) ->
      case canonical(V6(value)) {
        V4(_) as v4 -> #(v4, True)
        v6 -> #(v6, False)
      }
  }
}

// Only ::ffff:0:0/96 embeds an IPv4 address.
fn canonical(address: Address) -> Address {
  case address {
    V6(value) ->
      case int.bitwise_shift_right(value, 32) == 0xffff {
        True -> V4(int.bitwise_and(value, 0xffffffff))
        False -> address
      }
    V4(_) -> address
  }
}

// Unicode White_Space, the set Rust's str::trim trims.
fn trim_white_space(text: String) -> String {
  text
  |> string.to_utf_codepoints
  |> list.map(string.utf_codepoint_to_int)
  |> drop_white_space
  |> list.reverse
  |> drop_white_space
  |> list.reverse
  |> list.filter_map(string.utf_codepoint)
  |> string.from_utf_codepoints
}

fn drop_white_space(codes: List(Int)) -> List(Int) {
  case codes {
    [code, ..rest] ->
      case is_white_space(code) {
        True -> drop_white_space(rest)
        False -> codes
      }
    [] -> []
  }
}

fn is_white_space(code: Int) -> Bool {
  { code >= 0x09 && code <= 0x0d }
  || code == 0x20
  || code == 0x85
  || code == 0xa0
  || code == 0x1680
  || { code >= 0x2000 && code <= 0x200a }
  || code == 0x2028
  || code == 0x2029
  || code == 0x202f
  || code == 0x205f
  || code == 0x3000
}

fn guard(refuse: Bool, next: fn() -> Result(a, Nil)) -> Result(a, Nil) {
  case refuse {
    True -> Error(Nil)
    False -> next()
  }
}

type Family {
  Family4
  Family6
}

@external(erlang, "mist_pal_ffi", "parse_address")
fn ffi_parse_address(text: String) -> Result(#(Family, Int), Nil)

pub fn address_to_string(address: Address) -> String {
  ffi_address_to_string(address)
}

@external(erlang, "mist_pal_ffi", "address_to_string")
fn ffi_address_to_string(address: Address) -> String

/// The peer of a mist connection.
pub fn from_mist(address: mist.IpAddress) -> Address {
  case address {
    mist.IpV4(a, b, c, d) -> V4(groups([a, b, c, d], 8))
    mist.IpV6(a, b, c, d, e, f, g, h) ->
      canonical(V6(groups([a, b, c, d, e, f, g, h], 16)))
  }
}

fn groups(parts: List(Int), width: Int) -> Int {
  list.fold(parts, 0, fn(acc, part) {
    int.bitwise_or(int.bitwise_shift_left(acc, width), part)
  })
}

/// The walk is anchored at the socket peer, the one fact a client cannot
/// forge: a peer outside the trust set IS the client and the header is
/// ignored wholly. A trusted peer walks the x-forwarded-for entries right to
/// left (rightmost were appended last, by the proxies closest to us),
/// skipping trusted entries; the first untrusted entry is the client. A
/// chain exhausted with everything trusted yields the leftmost entry; a
/// malformed entry stops the walk at the last vetted position.
///
/// Headers as gleam_http holds them, names lowercased.
pub fn derive_client(
  peer: Option(Address),
  headers: List(#(String, String)),
  trusted: TrustedProxies,
) -> DerivedClient {
  let forwarded = list.key_filter(headers, "x-forwarded-for")
  case peer {
    None -> DerivedClient(None, Unknown)
    Some(peer) ->
      case contains(trusted, peer), forwarded {
        False, [] -> DerivedClient(Some(peer), DirectPeer)
        False, _ -> DerivedClient(Some(peer), UntrustedHeaderIgnored)
        True, _ ->
          // Split each value on its own: a proxy appends to the client's
          // value in place, so a byte the client sent poisons only its own
          // element.
          list.flat_map(forwarded, string.split(_, ","))
          |> list.reverse
          |> walk(peer, trusted)
      }
  }
}

fn walk(
  entries: List(String),
  client: Address,
  trusted: TrustedProxies,
) -> DerivedClient {
  case entries {
    [] -> DerivedClient(Some(client), TrustedTier)
    [entry, ..rest] ->
      case parse_entry(entry) {
        Error(Nil) -> DerivedClient(Some(client), TrustedTier)
        Ok(address) ->
          case contains(trusted, address) {
            False -> DerivedClient(Some(address), Forwarded)
            True -> walk(rest, address, trusted)
          }
      }
  }
}

// One x-forwarded-for element reduced to its canonical address: "v4",
// "v4:port", "[v6]", "[v6]:port", bare v6, a v6 zone id dropped. Anything
// else ("unknown", obfuscated tokens, empty, garbage) is malformed.
fn parse_entry(entry: String) -> Result(Address, Nil) {
  // A header value is bytes, not necessarily text.
  use entry <- result.try(bit_array.to_string(bit_array.from_string(entry)))
  let entry = trim_blanks(entry)
  use text <- result.try(case entry {
    "[" <> rest -> {
      use #(inner, port) <- result.try(string.split_once(rest, "]"))
      case port {
        "" -> Ok(drop_zone(inner))
        ":" <> digits ->
          case all_digits(digits) {
            True -> Ok(drop_zone(inner))
            False -> Error(Nil)
          }
        _ -> Error(Nil)
      }
    }
    _ ->
      case list.length(string.split(entry, ":")) - 1 {
        0 -> Ok(entry)
        1 -> {
          let assert Ok(#(address, port)) = string.split_once(entry, ":")
          case all_digits(port) {
            True -> Ok(address)
            False -> Error(Nil)
          }
        }
        _ -> Ok(drop_zone(entry))
      }
  })
  parse_address(text)
}

fn trim_blanks(text: String) -> String {
  case text {
    " " <> rest | "\t" <> rest -> trim_blanks(rest)
    _ ->
      case string.ends_with(text, " ") || string.ends_with(text, "\t") {
        True -> trim_blanks(string.drop_end(text, 1))
        False -> text
      }
  }
}

fn all_digits(text: String) -> Bool {
  text != ""
  && string.to_utf_codepoints(text)
  |> list.all(fn(c) {
    let code = string.utf_codepoint_to_int(c)
    code >= 48 && code <= 57
  })
}

// RFC 4007 "%zone" suffix: link scope on this box, not client identity. A
// bare trailing '%' is left for the parser to reject.
fn drop_zone(text: String) -> String {
  case string.split_once(text, "%") {
    Ok(#(address, zone)) if zone != "" -> address
    _ -> text
  }
}

pub fn source_label(source: ClientSource) -> String {
  case source {
    DirectPeer -> "direct_peer"
    UntrustedHeaderIgnored -> "untrusted_header_ignored"
    Forwarded -> "forwarded"
    TrustedTier -> "trusted_tier"
    Unknown -> "unknown"
  }
}
