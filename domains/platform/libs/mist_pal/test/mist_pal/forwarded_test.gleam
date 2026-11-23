//// The ADR-0012 walk, case for case with server_pal's forwarded.rs, so the
//// Gleam rail keys on the same client the Rust and C++ rails do.

import gleam/list
import gleam/option.{None, Some}
import gleam/string
import gleeunit/should
import mist_pal/forwarded.{
  type DerivedClient, type TrustedProxies, DerivedClient, DirectPeer, Forwarded,
  TrustedTier, Unknown, UntrustedHeaderIgnored,
}

fn trust(cidrs: List(String)) -> TrustedProxies {
  let assert Ok(trusted) = forwarded.parse(cidrs)
  trusted
}

fn ip(text: String) -> forwarded.Address {
  let assert Ok(address) = forwarded.parse_address(text)
  address
}

fn headers(xff: List(String)) -> List(#(String, String)) {
  list.map(xff, fn(value) { #("x-forwarded-for", value) })
}

fn walk(
  peer: String,
  xff: List(String),
  trusted: TrustedProxies,
) -> DerivedClient {
  forwarded.derive_client(Some(ip(peer)), headers(xff), trusted)
}

// The derived address as text, "" for none.
fn client(peer: String, xff: List(String), trusted: TrustedProxies) -> String {
  case walk(peer, xff, trusted).address {
    Some(address) -> forwarded.address_to_string(address)
    None -> ""
  }
}

@external(erlang, "mist_pal_test_ffi", "with_byte_255")
fn with_byte_255(text: String) -> String

pub fn matches_v4_and_v6_cidrs_test() {
  let t = trust(["10.0.0.0/8", "2001:db8::/32"])
  forwarded.contains(t, ip("10.255.0.1")) |> should.be_true
  forwarded.contains(t, ip("11.0.0.1")) |> should.be_false
  forwarded.contains(t, ip("2001:db8:ffff::1")) |> should.be_true
  forwarded.contains(t, ip("2001:db9::1")) |> should.be_false
}

pub fn mid_byte_prefixes_split_where_they_say_test() {
  let t = trust(["172.28.0.0/14"])
  forwarded.contains(t, ip("172.31.255.255")) |> should.be_true
  forwarded.contains(t, ip("172.32.0.0")) |> should.be_false
  forwarded.contains(t, ip("172.27.255.255")) |> should.be_false
}

pub fn a_bare_address_is_a_host_route_test() {
  let t = trust(["172.28.0.2"])
  forwarded.contains(t, ip("172.28.0.2")) |> should.be_true
  forwarded.contains(t, ip("172.28.0.3")) |> should.be_false
}

pub fn a_zero_prefix_trusts_the_whole_family_and_only_it_test() {
  let t = trust(["0.0.0.0/0"])
  forwarded.contains(t, ip("203.0.113.9")) |> should.be_true
  forwarded.contains(t, ip("2001:db8::1")) |> should.be_false
}

pub fn host_bits_in_the_base_match_as_if_masked_test() {
  forwarded.contains(trust(["10.1.2.3/8"]), ip("10.9.9.9")) |> should.be_true
}

// A dual-stack listener reports ::ffff:10.0.0.1; a v4 CIDR must match it,
// and a CIDR written in mapped form must match plain v4 with its prefix moved
// across the /96 mapping range.
pub fn v4_mapped_v6_is_the_embedded_v4_everywhere_test() {
  let v4 = trust(["10.0.0.0/8"])
  forwarded.contains(v4, ip("::ffff:10.0.0.1")) |> should.be_true
  forwarded.contains(v4, ip("::ffff:11.0.0.1")) |> should.be_false
  let mapped = trust(["::ffff:10.0.0.0/104"])
  forwarded.contains(mapped, ip("10.1.2.3")) |> should.be_true
  forwarded.contains(mapped, ip("10.128.0.1")) |> should.be_true
  forwarded.contains(mapped, ip("11.0.0.1")) |> should.be_false
  let whole = trust(["::ffff:0.0.0.0/96"])
  forwarded.contains(whole, ip("203.0.113.9")) |> should.be_true
  forwarded.contains(whole, ip("2001:db8::1")) |> should.be_false
  let host = trust(["::ffff:10.0.0.1"])
  forwarded.contains(host, ip("10.0.0.1")) |> should.be_true
  forwarded.contains(host, ip("10.0.0.2")) |> should.be_false
}

// Only ::ffff:0:0/96 embeds v4. The deprecated v4-compatible form must not
// inherit v4 trust, or an attacker-written entry walks as a hop.
pub fn v4_compatible_v6_is_not_the_embedded_v4_test() {
  forwarded.contains(trust(["10.0.0.0/8"]), ip("::10.0.0.1"))
  |> should.be_false
}

pub fn malformed_configuration_is_an_error_naming_the_entry_test() {
  [
    "10.0.0.0/33", "2001:db8::/129", "10.0.0.0/", "10.0.0.0/8/8", "10.0.0.0/ 8",
    "10.0.0.0/+8", "10.0.0.0/0008", "gateway/8", "", "010.0.0.0/8",
    "fe80::1%eth0/64", "fe80::1%eth0", "::ffff:10.0.0.0/64",
    "::ffff:10.0.0.0/95", "::ffff:10.0.0.0/129",
  ]
  |> list.each(fn(bad) {
    let assert Error(message) = forwarded.parse([bad])
    string.contains(message, "\"" <> bad <> "\"") |> should.be_true
  })
  forwarded.parse(["10.0.0.0/8", "bogus/8"]) |> should.be_error
}

// Unset is the deliberate direct-connect statement; set-but-empty or
// malformed must fail startup rather than collapse proxied traffic onto one
// key.
pub fn the_env_value_is_read_as_aura_reads_it_test() {
  let assert Ok(unset) = forwarded.from_env_value(Error(Nil))
  forwarded.contains(unset, ip("172.28.0.2")) |> should.be_false

  ["", " ", ",", " , "]
  |> list.each(fn(empty) {
    forwarded.from_env_value(Ok(empty)) |> should.be_error
  })
  forwarded.from_env_value(Ok("172.28.0.2,bogus")) |> should.be_error

  let assert Ok(listed) =
    forwarded.from_env_value(Ok(" 10.0.0.0/8, 172.28.0.2 ,,2600:1f00::/24,"))
  forwarded.contains(listed, ip("10.1.1.1")) |> should.be_true
  forwarded.contains(listed, ip("172.28.0.2")) |> should.be_true
  forwarded.contains(listed, ip("2600:1f00::1")) |> should.be_true
  forwarded.contains(listed, ip("172.28.0.3")) |> should.be_false
}

// The direct-connect spoof: whatever an untrusted peer wrote into the header
// is noise, and the peer is the client.
pub fn an_untrusted_peer_is_the_client_and_the_header_is_ignored_test() {
  let t = trust(["10.0.0.0/8"])
  walk("203.0.113.9", ["198.51.100.7"], t)
  |> should.equal(DerivedClient(Some(ip("203.0.113.9")), UntrustedHeaderIgnored))
  walk("203.0.113.9", [], t)
  |> should.equal(DerivedClient(Some(ip("203.0.113.9")), DirectPeer))
}

pub fn none_makes_even_a_proxy_the_client_test() {
  walk("172.28.0.2", ["203.0.113.9"], forwarded.none())
  |> should.equal(DerivedClient(Some(ip("172.28.0.2")), UntrustedHeaderIgnored))
}

pub fn a_trusted_peer_yields_the_rightmost_untrusted_entry_test() {
  walk("10.0.0.1", ["203.0.113.9"], trust(["10.0.0.0/8"]))
  |> should.equal(DerivedClient(Some(ip("203.0.113.9")), Forwarded))
}

// Caddy appends rather than replaces, so whatever the client wrote arrives
// ahead of the real entry. The walk stops before reaching it.
pub fn spoofed_prefix_entries_never_win_test() {
  client("10.0.0.1", ["6.6.6.6, 7.7.7.7, 203.0.113.9"], trust(["10.0.0.0/8"]))
  |> should.equal("203.0.113.9")
}

pub fn a_chain_of_trusted_tiers_skips_to_the_first_untrusted_test() {
  client(
    "10.0.0.1",
    ["6.6.6.6, 203.0.113.9, 10.0.0.3, 10.0.0.2"],
    trust(["10.0.0.0/8"]),
  )
  |> should.equal("203.0.113.9")
}

pub fn an_exhausted_all_trusted_chain_yields_the_leftmost_entry_test() {
  walk("10.0.0.1", ["10.0.0.2, 10.0.0.3"], trust(["10.0.0.0/8"]))
  |> should.equal(DerivedClient(Some(ip("10.0.0.2")), TrustedTier))
}

pub fn a_trusted_peer_with_no_header_is_the_client_test() {
  walk("10.0.0.1", [], trust(["10.0.0.0/8"]))
  |> should.equal(DerivedClient(Some(ip("10.0.0.1")), TrustedTier))
}

// Garbage is never trusted and never reached past: the walk falls back to
// the nearest vetted hop.
pub fn a_malformed_entry_stops_the_walk_at_the_last_vetted_position_test() {
  let t = trust(["10.0.0.0/8"])
  walk("10.0.0.1", ["203.0.113.9, unknown, 10.0.0.2"], t)
  |> should.equal(DerivedClient(Some(ip("10.0.0.2")), TrustedTier))
  client("10.0.0.1", ["not-an-ip"], t) |> should.equal("10.0.0.1")
  client("10.0.0.1", ["203.0.113.9,,10.0.0.2"], t) |> should.equal("10.0.0.2")
  client("10.0.0.1", ["203.0.113.9, 010.0.0.2"], t) |> should.equal("10.0.0.1")
}

// A byte that is not UTF-8 poisons only its own element: the entry the proxy
// appended after it still counts, and the walk stops at it otherwise.
pub fn an_opaque_byte_poisons_only_its_own_element_test() {
  let t = trust(["10.0.0.0/8"])
  forwarded.derive_client(
    Some(ip("10.0.0.1")),
    [#("x-forwarded-for", with_byte_255("198.51.100.7") <> ", 203.0.113.9")],
    t,
  )
  |> should.equal(DerivedClient(Some(ip("203.0.113.9")), Forwarded))
  forwarded.derive_client(
    Some(ip("10.0.0.1")),
    [
      #("x-forwarded-for", "203.0.113.9"),
      #("x-forwarded-for", with_byte_255("198.51.100.7")),
    ],
    t,
  )
  |> should.equal(DerivedClient(Some(ip("10.0.0.1")), TrustedTier))
}

pub fn trusted_hops_are_recognized_in_any_entry_form_test() {
  let t = trust(["10.0.0.0/8"])
  client("10.0.0.1", ["203.0.113.9, 10.0.0.2:8080"], t)
  |> should.equal("203.0.113.9")
  client("10.0.0.1", ["203.0.113.9, ::ffff:10.0.0.2"], t)
  |> should.equal("203.0.113.9")
  client("10.0.0.1", ["::ffff:203.0.113.9"], t) |> should.equal("203.0.113.9")
  // v4-compatible is an untrusted v6 client, not a trusted v4 hop.
  walk("10.0.0.1", ["203.0.113.9, ::10.0.0.2"], t).address
  |> should.equal(Some(ip("::10.0.0.2")))
}

// The inherent recursive-walk caveat: a client inside the trust set is
// skipped as a hop and can forge its ancestry. Trust only networks proxies
// alone occupy.
pub fn a_client_inside_the_trust_set_can_forge_its_ancestry_test() {
  client("10.0.0.1", ["6.6.6.6, 10.0.0.5"], trust(["10.0.0.0/8"]))
  |> should.equal("6.6.6.6")
}

pub fn entries_tolerate_the_forms_real_proxies_emit_test() {
  let t = trust(["10.0.0.0/8"])
  client("10.0.0.1", [" 203.0.113.9:52814 "], t) |> should.equal("203.0.113.9")
  client("10.0.0.1", ["\t203.0.113.9\t"], t) |> should.equal("203.0.113.9")
  client("10.0.0.1", ["[2001:db8::1]:443"], t) |> should.equal("2001:db8::1")
  client("10.0.0.1", ["[2001:db8::1]"], t) |> should.equal("2001:db8::1")
  client("10.0.0.1", ["2001:DB8::1"], t) |> should.equal("2001:db8::1")
  client("10.0.0.1", ["::"], t) |> should.equal("::")
  // Trailing junk is malformed, not half-parsed.
  [
    "[2001:db8::1]x", "[2001:db8::1]:abc", "[2001:db8::1]:", "[2001:db8::1",
    "203.0.113.9:port", "203.0.113.9:", "203.0.113.9 x",
  ]
  |> list.each(fn(entry) {
    client("10.0.0.1", [entry], t) |> should.equal("10.0.0.1")
  })
}

// The zone disambiguates link scope, not identity: dropped, so link-local
// clients key as the bare address.
pub fn zone_ids_are_dropped_from_v6_entries_test() {
  let t = trust(["10.0.0.0/8"])
  client("10.0.0.1", ["fe80::2%eth1"], t) |> should.equal("fe80::2")
  client("10.0.0.1", ["[fe80::2%eth1]:443"], t) |> should.equal("fe80::2")
  // A bare trailing '%' is malformed.
  client("10.0.0.1", ["fe80::3%"], t) |> should.equal("10.0.0.1")
}

// Proxies may add a header instead of appending; RFC 9110 list semantics
// make that equivalent, and the walk crosses headers.
pub fn multiple_headers_join_in_order_test() {
  let t = trust(["10.0.0.0/8"])
  client("10.0.0.1", ["198.51.100.7", "203.0.113.9, 10.0.0.2"], t)
  |> should.equal("203.0.113.9")
  client("10.0.0.1", ["198.51.100.7, 203.0.113.9", "10.0.0.2, 10.0.0.3"], t)
  |> should.equal("203.0.113.9")
}

pub fn v6_and_mapped_peers_derive_bare_canonical_addresses_test() {
  let v6 = trust(["2001:db8::/32"])
  client("2001:db8::1", ["203.0.113.9"], v6) |> should.equal("203.0.113.9")
  client("2001:db9::1", ["203.0.113.9"], v6) |> should.equal("2001:db9::1")
  let v4 = trust(["10.0.0.0/8"])
  client("::ffff:10.0.0.1", ["203.0.113.9"], v4) |> should.equal("203.0.113.9")
  client("::ffff:203.0.113.9", [], v4) |> should.equal("203.0.113.9")
}

pub fn no_peer_derives_nothing_test() {
  forwarded.derive_client(None, headers(["203.0.113.9"]), trust(["10.0.0.0/8"]))
  |> should.equal(DerivedClient(None, Unknown))
}

pub fn source_spellings_match_the_cpp_access_log_test() {
  [DirectPeer, UntrustedHeaderIgnored, Forwarded, TrustedTier, Unknown]
  |> list.map(forwarded.source_label)
  |> should.equal([
    "direct_peer", "untrusted_header_ignored", "forwarded", "trusted_tier",
    "unknown",
  ])
}

// inet reads an octet with a sign; Rust's parser, and so server_pal, does
// not.
pub fn a_signed_octet_is_malformed_test() {
  let t = trust(["10.0.0.0/8"])
  [
    "+1.2.3.4",
    "1.2.3.-0",
    "[1.+2.3.4]:80",
    "::FFFF:1.2.+3.4",
    "1:2:3:4:5:6:1.2.3.+4",
  ]
  |> list.each(fn(entry) {
    client("10.0.0.1", [entry], t) |> should.equal("10.0.0.1")
  })
  forwarded.parse(["+10.0.0.0/8"]) |> should.be_error
  forwarded.parse(["1.-0.0.0/8"]) |> should.be_error
}

// Entries are trimmed of Unicode White_Space, as Rust's str::trim trims.
pub fn env_entries_are_trimmed_of_unicode_white_space_test() {
  forwarded.from_env_value(Ok("10.0.0.0/8,\u{00A0}")) |> should.be_ok
  forwarded.from_env_value(Ok("10.0.0.0/8\u{3000}")) |> should.be_ok
  forwarded.from_env_value(Ok("10.0.0.0/8\u{200E}")) |> should.be_error
}

// An IPv4-mapped address built by hand is still the V4 it embeds.
pub fn contains_reads_a_mapped_v6_as_its_v4_test() {
  forwarded.contains(trust(["10.0.0.0/8"]), forwarded.V6(0xffff_0a000001))
  |> should.be_true
}
