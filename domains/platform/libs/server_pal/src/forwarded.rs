//! Client-address derivation behind a reverse proxy: opal-cpp ADR-0012's
//! rightmost-untrusted walk, so the Rust rail keys on the same client the C++
//! rail does (`opal/http/forwarded.h`, aura's `TrustedProxiesFromEnv`).

use std::net::IpAddr;

use axum::http::HeaderMap;

/// The deployment's reverse-proxy trust boundary: the networks whose
/// x-forwarded-for entries count. There is deliberately no `Default`:
/// behind a proxy the accidental empty set collapses every caller onto the
/// proxy's one key, so "trust nothing" is spelled `none()`.
#[derive(Clone, Debug)]
pub struct TrustedProxies {
    networks: Vec<(IpAddr, u8)>,
}

impl TrustedProxies {
    pub fn none() -> Self {
        TrustedProxies { networks: vec![] }
    }

    /// CIDR and bare-address (host route) entries, or an error naming the
    /// first malformed one: a trust boundary that does not parse must fail
    /// startup, not silently widen or narrow.
    pub fn parse<S: AsRef<str>>(cidrs: &[S]) -> Result<Self, String> {
        let networks = cidrs
            .iter()
            .map(|cidr| parse_network(cidr.as_ref()))
            .collect::<Result<_, _>>()?;
        Ok(TrustedProxies { networks })
    }

    /// `TRUSTED_PROXY_CIDRS`, read as aura reads it.
    pub(crate) fn from_env() -> Result<Self, String> {
        Self::from_env_var(std::env::var("TRUSTED_PROXY_CIDRS"))
    }

    /// Set but not UTF-8 is malformed, not absent.
    fn from_env_var(var: Result<String, std::env::VarError>) -> Result<Self, String> {
        match var {
            Ok(value) => Self::from_env_value(Some(&value)),
            Err(std::env::VarError::NotPresent) => Self::from_env_value(None),
            Err(std::env::VarError::NotUnicode(_)) => {
                Err("TRUSTED_PROXY_CIDRS is not valid UTF-8".into())
            }
        }
    }

    /// Unset is the deliberate direct-connect statement; set-but-empty is
    /// refused, since it can only be a config typo behind a proxy.
    fn from_env_value(value: Option<&str>) -> Result<Self, String> {
        let Some(value) = value else {
            return Ok(Self::none());
        };
        let cidrs: Vec<&str> = value
            .split(',')
            .map(str::trim)
            .filter(|entry| !entry.is_empty())
            .collect();
        if cidrs.is_empty() {
            return Err(
                "TRUSTED_PROXY_CIDRS is set but empty; unset it to serve direct-connect".into(),
            );
        }
        Self::parse(&cidrs).map_err(|e| format!("invalid TRUSTED_PROXY_CIDRS: {e}"))
    }

    /// IPv4-mapped IPv6 matches as the embedded IPv4.
    pub(crate) fn contains(&self, address: IpAddr) -> bool {
        let address = address.to_canonical();
        self.networks
            .iter()
            .any(|&(base, prefix)| in_network(base, prefix, address))
    }
}

fn parse_network(cidr: &str) -> Result<(IpAddr, u8), String> {
    let (base_text, prefix_text) = match cidr.split_once('/') {
        Some((base, prefix)) => (base, Some(prefix)),
        None => (cidr, None),
    };
    let base: IpAddr = base_text
        .parse()
        .map_err(|_| format!("TrustedProxies: unparseable address in {cidr:?}"))?;
    // A base written as IPv4-mapped IPv6 is the embedded IPv4, so its
    // prefix moves across the /96 mapping range with it.
    let mapped = base.is_ipv6() && base.to_canonical().is_ipv4();
    let base = base.to_canonical();
    let max_bits: u16 = if base.is_ipv4() { 32 } else { 128 };
    let Some(prefix_text) = prefix_text else {
        return Ok((base, max_bits as u8));
    };
    if prefix_text.is_empty()
        || prefix_text.len() > 3
        || !prefix_text.bytes().all(|b| b.is_ascii_digit())
    {
        return Err(format!("TrustedProxies: malformed prefix in {cidr:?}"));
    }
    let written: u16 = prefix_text.parse().expect("1-3 ASCII digits");
    let prefix = if mapped {
        written.checked_sub(96)
    } else {
        Some(written)
    };
    match prefix {
        Some(prefix) if prefix <= max_bits => Ok((base, prefix as u8)),
        _ => Err(format!("TrustedProxies: prefix out of range in {cidr:?}")),
    }
}

fn in_network(base: IpAddr, prefix: u8, address: IpAddr) -> bool {
    match (base, address) {
        (IpAddr::V4(base), IpAddr::V4(address)) => {
            let mask = u32::MAX.checked_shl(32 - u32::from(prefix)).unwrap_or(0);
            u32::from(base) & mask == u32::from(address) & mask
        }
        (IpAddr::V6(base), IpAddr::V6(address)) => {
            let mask = u128::MAX.checked_shl(128 - u32::from(prefix)).unwrap_or(0);
            u128::from(base) & mask == u128::from(address) & mask
        }
        _ => false,
    }
}

/// Which path produced a derived client — opal's `DerivedClient::Source`,
/// spelled as its access log spells it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum ClientSource {
    /// Untrusted peer, no x-forwarded-for: the client itself.
    DirectPeer,
    /// Untrusted peer; the header was present and ignored.
    UntrustedHeaderIgnored,
    /// The trusted walk ended on the client's entry.
    Forwarded,
    /// The walk never left the trust set (no header, chain exhausted, or
    /// stopped by a malformed entry).
    TrustedTier,
    /// No peer at all; there is no address.
    Unknown,
}

impl ClientSource {
    pub(crate) fn as_str(self) -> &'static str {
        match self {
            ClientSource::DirectPeer => "direct_peer",
            ClientSource::UntrustedHeaderIgnored => "untrusted_header_ignored",
            ClientSource::Forwarded => "forwarded",
            ClientSource::TrustedTier => "trusted_tier",
            ClientSource::Unknown => "unknown",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct DerivedClient {
    pub(crate) address: Option<IpAddr>,
    pub(crate) source: ClientSource,
}

/// The walk is anchored at the socket peer, the one fact a client cannot
/// forge: a peer outside the trust set IS the client and the header is
/// ignored wholly. A trusted peer walks the x-forwarded-for entries right
/// to left (rightmost were appended last, by the proxies closest to us),
/// skipping trusted entries; the first untrusted entry is the client. A
/// chain exhausted with everything trusted yields the leftmost entry; a
/// malformed entry stops the walk at the last vetted position.
pub(crate) fn derive_client(
    peer: Option<IpAddr>,
    headers: &HeaderMap,
    trusted: &TrustedProxies,
) -> DerivedClient {
    let Some(peer) = peer else {
        return DerivedClient {
            address: None,
            source: ClientSource::Unknown,
        };
    };
    let mut client = peer.to_canonical();
    if !trusted.contains(client) {
        let source = if headers.contains_key(X_FORWARDED_FOR) {
            ClientSource::UntrustedHeaderIgnored
        } else {
            ClientSource::DirectPeer
        };
        return DerivedClient {
            address: Some(client),
            source,
        };
    }
    // Split on raw bytes: a proxy appends to the client's value in place, so
    // a byte the client sent must poison only its own element.
    let entries: Vec<Option<&str>> = headers
        .get_all(X_FORWARDED_FOR)
        .iter()
        .flat_map(|value| value.as_bytes().split(|&b| b == b','))
        .map(|element| std::str::from_utf8(element).ok())
        .collect();
    for entry in entries.into_iter().rev() {
        let Some(address) = entry.and_then(parse_entry) else {
            break;
        };
        client = address;
        if !trusted.contains(client) {
            return DerivedClient {
                address: Some(client),
                source: ClientSource::Forwarded,
            };
        }
    }
    DerivedClient {
        address: Some(client),
        source: ClientSource::TrustedTier,
    }
}

const X_FORWARDED_FOR: &str = "x-forwarded-for";

/// One x-forwarded-for element reduced to its canonical address — "v4",
/// "v4:port", "[v6]", "[v6]:port", bare v6, a v6 zone id dropped — or None
/// for anything else ("unknown", obfuscated tokens, empty, garbage).
fn parse_entry(entry: &str) -> Option<IpAddr> {
    let entry = entry.trim_matches([' ', '\t']);
    let text = if let Some(rest) = entry.strip_prefix('[') {
        let (inner, port) = rest.split_once(']')?;
        if !port.is_empty() && !port.strip_prefix(':').is_some_and(all_digits) {
            return None;
        }
        drop_zone(inner)
    } else {
        match entry.matches(':').count() {
            0 => entry,
            1 => {
                let (address, port) = entry.split_once(':')?;
                if !all_digits(port) {
                    return None;
                }
                address
            }
            _ => drop_zone(entry),
        }
    };
    text.parse::<IpAddr>().ok().map(|a| a.to_canonical())
}

fn all_digits(text: &str) -> bool {
    !text.is_empty() && text.bytes().all(|b| b.is_ascii_digit())
}

/// RFC 4007 "%zone" suffix: link scope on this box, not client identity. A
/// bare trailing '%' is left for the parser to reject.
fn drop_zone(text: &str) -> &str {
    match text.split_once('%') {
        Some((address, zone)) if !zone.is_empty() => address,
        _ => text,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn trust(cidrs: &[&str]) -> TrustedProxies {
        TrustedProxies::parse(cidrs).unwrap_or_else(|e| panic!("{e}"))
    }

    fn ip(text: &str) -> IpAddr {
        text.parse().unwrap()
    }

    fn headers(xff: &[&str]) -> HeaderMap {
        let mut map = HeaderMap::new();
        for value in xff {
            map.append("x-forwarded-for", value.parse().unwrap());
        }
        map
    }

    fn derive(peer: &str, xff: &[&str], trusted: &TrustedProxies) -> DerivedClient {
        derive_client(Some(ip(peer)), &headers(xff), trusted)
    }

    /// The derived address as text, "" for none.
    fn client(peer: &str, xff: &[&str], trusted: &TrustedProxies) -> String {
        derive(peer, xff, trusted)
            .address
            .map(|a| a.to_string())
            .unwrap_or_default()
    }

    #[test]
    fn matches_v4_and_v6_cidrs() {
        let t = trust(&["10.0.0.0/8", "2001:db8::/32"]);
        assert!(t.contains(ip("10.255.0.1")));
        assert!(!t.contains(ip("11.0.0.1")));
        assert!(t.contains(ip("2001:db8:ffff::1")));
        assert!(!t.contains(ip("2001:db9::1")));
    }

    #[test]
    fn mid_byte_prefixes_split_where_they_say() {
        let t = trust(&["172.28.0.0/14"]);
        assert!(t.contains(ip("172.31.255.255")));
        assert!(!t.contains(ip("172.32.0.0")));
        assert!(!t.contains(ip("172.27.255.255")));
    }

    #[test]
    fn a_bare_address_is_a_host_route() {
        let t = trust(&["172.28.0.2"]);
        assert!(t.contains(ip("172.28.0.2")));
        assert!(!t.contains(ip("172.28.0.3")));
    }

    #[test]
    fn a_zero_prefix_trusts_the_whole_family_and_only_it() {
        let t = trust(&["0.0.0.0/0"]);
        assert!(t.contains(ip("203.0.113.9")));
        assert!(!t.contains(ip("2001:db8::1")));
    }

    #[test]
    fn host_bits_in_the_base_match_as_if_masked() {
        assert!(trust(&["10.1.2.3/8"]).contains(ip("10.9.9.9")));
    }

    /// A dual-stack listener reports ::ffff:10.0.0.1; a v4 CIDR must match
    /// it, and a CIDR written in mapped form must match plain v4 with its
    /// prefix moved across the /96 mapping range.
    #[test]
    fn v4_mapped_v6_is_the_embedded_v4_everywhere() {
        let v4 = trust(&["10.0.0.0/8"]);
        assert!(v4.contains(ip("::ffff:10.0.0.1")));
        assert!(!v4.contains(ip("::ffff:11.0.0.1")));
        let mapped = trust(&["::ffff:10.0.0.0/104"]);
        assert!(mapped.contains(ip("10.1.2.3")));
        assert!(mapped.contains(ip("10.128.0.1")));
        assert!(!mapped.contains(ip("11.0.0.1")));
        let whole = trust(&["::ffff:0.0.0.0/96"]);
        assert!(whole.contains(ip("203.0.113.9")));
        assert!(!whole.contains(ip("2001:db8::1")));
        let host = trust(&["::ffff:10.0.0.1"]);
        assert!(host.contains(ip("10.0.0.1")));
        assert!(!host.contains(ip("10.0.0.2")));
    }

    /// Only ::ffff:0:0/96 embeds v4. The deprecated v4-compatible form must
    /// not inherit v4 trust, or an attacker-written entry walks as a hop.
    #[test]
    fn v4_compatible_v6_is_not_the_embedded_v4() {
        assert!(!trust(&["10.0.0.0/8"]).contains(ip("::10.0.0.1")));
    }

    #[test]
    fn malformed_configuration_is_an_error() {
        for bad in [
            "10.0.0.0/33",
            "2001:db8::/129",
            "10.0.0.0/",
            "10.0.0.0/8/8",
            "10.0.0.0/ 8",
            "10.0.0.0/+8",
            "10.0.0.0/0008",
            "gateway/8",
            "",
            "010.0.0.0/8",
            "fe80::1%eth0/64",
            "::ffff:10.0.0.0/64",
            "::ffff:10.0.0.0/95",
            "::ffff:10.0.0.0/129",
        ] {
            let err = TrustedProxies::parse(&[bad])
                .err()
                .unwrap_or_else(|| panic!("expected a parse error for {bad:?}"));
            assert!(err.contains(bad), "{err:?} names the entry {bad:?}");
        }
        assert!(TrustedProxies::parse(&["10.0.0.0/8", "bogus/8"]).is_err());
    }

    /// Unset is the deliberate direct-connect statement; set-but-empty or
    /// malformed must fail startup rather than collapse proxied traffic onto
    /// one key. Splitting matches futility::env::ReadList.
    #[test]
    fn the_env_value_is_read_as_aura_reads_it() {
        let unset = TrustedProxies::from_env_value(None).unwrap();
        assert!(!unset.contains(ip("172.28.0.2")));

        for empty in ["", " ", ",", " , "] {
            assert!(
                TrustedProxies::from_env_value(Some(empty)).is_err(),
                "{empty:?} must refuse to start"
            );
        }
        assert!(TrustedProxies::from_env_value(Some("172.28.0.2,bogus")).is_err());

        let listed =
            TrustedProxies::from_env_value(Some(" 10.0.0.0/8, 172.28.0.2 ,,2600:1f00::/24,"))
                .unwrap();
        assert!(listed.contains(ip("10.1.1.1")));
        assert!(listed.contains(ip("172.28.0.2")));
        assert!(listed.contains(ip("2600:1f00::1")));
        assert!(!listed.contains(ip("172.28.0.3")));
    }

    /// The direct-connect spoof: whatever an untrusted peer wrote into the
    /// header is noise, and the peer is the client.
    #[test]
    fn an_untrusted_peer_is_the_client_and_the_header_is_ignored() {
        let t = trust(&["10.0.0.0/8"]);
        let d = derive("203.0.113.9", &["198.51.100.7"], &t);
        assert_eq!(d.address, Some(ip("203.0.113.9")));
        assert_eq!(d.source, ClientSource::UntrustedHeaderIgnored);
        let d = derive("203.0.113.9", &[], &t);
        assert_eq!(d.address, Some(ip("203.0.113.9")));
        assert_eq!(d.source, ClientSource::DirectPeer);
    }

    #[test]
    fn none_makes_even_a_proxy_the_client() {
        let d = derive("172.28.0.2", &["203.0.113.9"], &TrustedProxies::none());
        assert_eq!(d.address, Some(ip("172.28.0.2")));
        assert_eq!(d.source, ClientSource::UntrustedHeaderIgnored);
    }

    #[test]
    fn a_trusted_peer_yields_the_rightmost_untrusted_entry() {
        let t = trust(&["10.0.0.0/8"]);
        let d = derive("10.0.0.1", &["203.0.113.9"], &t);
        assert_eq!(d.address, Some(ip("203.0.113.9")));
        assert_eq!(d.source, ClientSource::Forwarded);
    }

    /// Caddy appends rather than replaces, so whatever the client wrote
    /// arrives ahead of the real entry. The walk stops before reaching it.
    #[test]
    fn spoofed_prefix_entries_never_win() {
        let t = trust(&["10.0.0.0/8"]);
        assert_eq!(
            client("10.0.0.1", &["6.6.6.6, 7.7.7.7, 203.0.113.9"], &t),
            "203.0.113.9"
        );
    }

    #[test]
    fn a_chain_of_trusted_tiers_skips_to_the_first_untrusted() {
        let t = trust(&["10.0.0.0/8"]);
        assert_eq!(
            client(
                "10.0.0.1",
                &["6.6.6.6, 203.0.113.9, 10.0.0.3, 10.0.0.2"],
                &t
            ),
            "203.0.113.9"
        );
    }

    #[test]
    fn an_exhausted_all_trusted_chain_yields_the_leftmost_entry() {
        let t = trust(&["10.0.0.0/8"]);
        let d = derive("10.0.0.1", &["10.0.0.2, 10.0.0.3"], &t);
        assert_eq!(d.address, Some(ip("10.0.0.2")));
        assert_eq!(d.source, ClientSource::TrustedTier);
    }

    #[test]
    fn a_trusted_peer_with_no_header_is_the_client() {
        let t = trust(&["10.0.0.0/8"]);
        let d = derive("10.0.0.1", &[], &t);
        assert_eq!(d.address, Some(ip("10.0.0.1")));
        assert_eq!(d.source, ClientSource::TrustedTier);
    }

    /// Garbage is never trusted and never reached past: the walk falls back
    /// to the nearest vetted hop.
    #[test]
    fn a_malformed_entry_stops_the_walk_at_the_last_vetted_position() {
        let t = trust(&["10.0.0.0/8"]);
        let beyond = derive("10.0.0.1", &["203.0.113.9, unknown, 10.0.0.2"], &t);
        assert_eq!(beyond.address, Some(ip("10.0.0.2")));
        assert_eq!(beyond.source, ClientSource::TrustedTier);
        assert_eq!(client("10.0.0.1", &["not-an-ip"], &t), "10.0.0.1");
        assert_eq!(
            client("10.0.0.1", &["203.0.113.9,,10.0.0.2"], &t),
            "10.0.0.2"
        );
        assert_eq!(
            client("10.0.0.1", &["203.0.113.9, 010.0.0.2"], &t),
            "10.0.0.1"
        );
    }

    /// A header value that is not visible ASCII cannot be read as entries,
    /// so it stops the walk like any other malformed entry rather than being
    /// skipped to reach the readable header before it.
    #[test]
    fn an_opaque_header_value_stops_the_walk() {
        let t = trust(&["10.0.0.0/8"]);
        let mut map = headers(&["203.0.113.9"]);
        map.append(
            "x-forwarded-for",
            axum::http::HeaderValue::from_bytes(b"198.51.100.7\xff").unwrap(),
        );
        let d = derive_client(Some(ip("10.0.0.1")), &map, &t);
        assert_eq!(d.address, Some(ip("10.0.0.1")));
        assert_eq!(d.source, ClientSource::TrustedTier);
    }

    /// Proxies append to the client's header in place, so a byte the
    /// client sent poisons only its own element: the entry the proxy
    /// appended after it still counts.
    #[test]
    fn an_opaque_byte_poisons_only_its_own_element() {
        let t = trust(&["10.0.0.0/8"]);
        let mut map = HeaderMap::new();
        map.append(
            "x-forwarded-for",
            axum::http::HeaderValue::from_bytes(b"198.51.100.7\xff, 203.0.113.9").unwrap(),
        );
        let d = derive_client(Some(ip("10.0.0.1")), &map, &t);
        assert_eq!(d.address, Some(ip("203.0.113.9")));
        assert_eq!(d.source, ClientSource::Forwarded);
    }

    /// A value that is set but not UTF-8 is malformed, not absent.
    #[test]
    fn a_non_unicode_env_value_refuses_to_start() {
        let not_unicode = std::env::VarError::NotUnicode(std::ffi::OsString::from("x"));
        assert!(TrustedProxies::from_env_var(Err(not_unicode)).is_err());
        let unset = TrustedProxies::from_env_var(Err(std::env::VarError::NotPresent)).unwrap();
        assert!(!unset.contains(ip("172.28.0.2")));
        let set = TrustedProxies::from_env_var(Ok("172.28.0.2".into())).unwrap();
        assert!(set.contains(ip("172.28.0.2")));
    }

    #[test]
    fn trusted_hops_are_recognized_in_any_entry_form() {
        let t = trust(&["10.0.0.0/8"]);
        assert_eq!(
            client("10.0.0.1", &["203.0.113.9, 10.0.0.2:8080"], &t),
            "203.0.113.9"
        );
        assert_eq!(
            client("10.0.0.1", &["203.0.113.9, ::ffff:10.0.0.2"], &t),
            "203.0.113.9"
        );
        assert_eq!(
            client("10.0.0.1", &["::ffff:203.0.113.9"], &t),
            "203.0.113.9"
        );
        // v4-compatible is an untrusted v6 client, not a trusted v4 hop.
        // std formats it as ::a00:2 where inet_ntop writes ::10.0.0.2; the
        // address, and so the key, is the same.
        assert_eq!(
            derive("10.0.0.1", &["203.0.113.9, ::10.0.0.2"], &t).address,
            Some(ip("::10.0.0.2"))
        );
    }

    /// The inherent recursive-walk caveat: a client inside the trust set is
    /// skipped as a hop and can forge its ancestry. Trust only networks
    /// proxies alone occupy.
    #[test]
    fn a_client_inside_the_trust_set_can_forge_its_ancestry() {
        let t = trust(&["10.0.0.0/8"]);
        assert_eq!(client("10.0.0.1", &["6.6.6.6, 10.0.0.5"], &t), "6.6.6.6");
    }

    #[test]
    fn entries_tolerate_the_forms_real_proxies_emit() {
        let t = trust(&["10.0.0.0/8"]);
        assert_eq!(
            client("10.0.0.1", &[" 203.0.113.9:52814 "], &t),
            "203.0.113.9"
        );
        assert_eq!(client("10.0.0.1", &["\t203.0.113.9\t"], &t), "203.0.113.9");
        assert_eq!(
            client("10.0.0.1", &["[2001:db8::1]:443"], &t),
            "2001:db8::1"
        );
        assert_eq!(client("10.0.0.1", &["[2001:db8::1]"], &t), "2001:db8::1");
        assert_eq!(client("10.0.0.1", &["2001:DB8::1"], &t), "2001:db8::1");
        assert_eq!(client("10.0.0.1", &["::"], &t), "::");
        // Trailing junk is malformed, not half-parsed.
        assert_eq!(client("10.0.0.1", &["[2001:db8::1]x"], &t), "10.0.0.1");
        assert_eq!(client("10.0.0.1", &["[2001:db8::1]:abc"], &t), "10.0.0.1");
        assert_eq!(client("10.0.0.1", &["[2001:db8::1]:"], &t), "10.0.0.1");
        assert_eq!(client("10.0.0.1", &["[2001:db8::1"], &t), "10.0.0.1");
        assert_eq!(client("10.0.0.1", &["203.0.113.9:port"], &t), "10.0.0.1");
        assert_eq!(client("10.0.0.1", &["203.0.113.9:"], &t), "10.0.0.1");
        assert_eq!(client("10.0.0.1", &["203.0.113.9 x"], &t), "10.0.0.1");
    }

    /// The zone disambiguates link scope, not identity: dropped, so
    /// link-local clients key as the bare address.
    #[test]
    fn zone_ids_are_dropped_from_v6_entries() {
        let v4 = trust(&["10.0.0.0/8"]);
        assert_eq!(client("10.0.0.1", &["fe80::2%eth1"], &v4), "fe80::2");
        assert_eq!(client("10.0.0.1", &["[fe80::2%eth1]:443"], &v4), "fe80::2");
        // A bare trailing '%' is malformed.
        assert_eq!(client("10.0.0.1", &["fe80::3%"], &v4), "10.0.0.1");
    }

    /// Proxies may add a header instead of appending; RFC 9110 list
    /// semantics make that equivalent, and the walk crosses headers.
    #[test]
    fn multiple_headers_join_in_order() {
        let t = trust(&["10.0.0.0/8"]);
        assert_eq!(
            client("10.0.0.1", &["198.51.100.7", "203.0.113.9, 10.0.0.2"], &t),
            "203.0.113.9"
        );
        assert_eq!(
            client(
                "10.0.0.1",
                &["198.51.100.7, 203.0.113.9", "10.0.0.2, 10.0.0.3"],
                &t
            ),
            "203.0.113.9"
        );
    }

    #[test]
    fn v6_and_mapped_peers_derive_bare_canonical_addresses() {
        let v6 = trust(&["2001:db8::/32"]);
        assert_eq!(client("2001:db8::1", &["203.0.113.9"], &v6), "203.0.113.9");
        assert_eq!(client("2001:db9::1", &["203.0.113.9"], &v6), "2001:db9::1");
        let v4 = trust(&["10.0.0.0/8"]);
        assert_eq!(
            client("::ffff:10.0.0.1", &["203.0.113.9"], &v4),
            "203.0.113.9"
        );
        assert_eq!(client("::ffff:203.0.113.9", &[], &v4), "203.0.113.9");
    }

    #[test]
    fn no_peer_derives_nothing() {
        let d = derive_client(None, &headers(&["203.0.113.9"]), &trust(&["10.0.0.0/8"]));
        assert_eq!(d.address, None);
        assert_eq!(d.source, ClientSource::Unknown);
    }

    #[test]
    fn source_spellings_match_the_cpp_access_log() {
        assert_eq!(ClientSource::DirectPeer.as_str(), "direct_peer");
        assert_eq!(
            ClientSource::UntrustedHeaderIgnored.as_str(),
            "untrusted_header_ignored"
        );
        assert_eq!(ClientSource::Forwarded.as_str(), "forwarded");
        assert_eq!(ClientSource::TrustedTier.as_str(), "trusted_tier");
        assert_eq!(ClientSource::Unknown.as_str(), "unknown");
    }
}
