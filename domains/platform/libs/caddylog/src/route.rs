use crate::classify::fold;

/// Every `path` matcher in `deploy/consolidated/Caddyfile`, by site, spelled
/// as the Caddyfile spells it. A route token is one of a site's matchers or
/// `OTHER_ROUTE`, so the vocabulary is the gateway's `path` matchers and
/// nothing else: a site routed by `path_regexp` or a bare `handle`, which is
/// all of git.muchq.com, is route-blind on purpose, and its status and probe
/// factors carry what signal there is. The test below pins this table equal
/// to the Caddyfile in both directions. A trailing `/*` is a prefix matcher,
/// anything else is exact. No site's matchers overlap, so the first match
/// is the only one; the shape test below is what says so.
pub const ROUTES: &[(&str, &[&str])] = &[
    (
        "api.1d4.net",
        &[
            "/health",
            "/stats/v1/one_d4/*",
            "/v1/index",
            "/v1/index/*",
            "/v1/query",
        ],
    ),
    (
        "api.muchq.com",
        &[
            "/1d4/v1/health",
            "/1d4/v1/index",
            "/1d4/v1/index/*",
            "/1d4/v1/query",
            "/games/v2/play",
            "/games/v2/session",
            "/iili/v1/r/*",
            "/iili/v1/shorten",
            "/imagine/v1/blur",
            "/imagine/v1/edges",
            "/metrics/v1/*",
            "/microgpt/v1/chat",
            "/microgpt/v1/generate",
            "/mithril/v1/wordchain",
            "/portrait/v1/trace",
            "/stats/v1/*",
            "/v2/analyze",
        ],
    ),
    (
        "gpt.muchq.com",
        &["/microgpt/v1/chat", "/microgpt/v1/generate"],
    ),
    ("i.iili.uk", &["/r/*"]),
    ("mcp.1d4.net", &["/mcp"]),
];

/// The route of a request no matcher claims on its site: a scanner's path,
/// a typo, a Forgejo page, a site with no matchers. One token for all of
/// them, so the unrouted tail cannot mint a vocabulary entry per path.
pub const OTHER_ROUTE: &str = "other";

/// The Caddyfile matcher that claims `uri` on `host`, or `OTHER_ROUTE`. The
/// path is matched the way Caddy's `path` matcher matches it: percent
/// escapes decoded, `.` and `..` segments and doubled slashes cleaned, a
/// trailing slash kept, then compared case-insensitively. The host loses
/// any port and its case.
pub fn route_of(host: &str, uri: &str) -> &'static str {
    let host = fold(host.split(':').next().unwrap_or_default());
    let Some((_, routes)) = ROUTES.iter().find(|(site, _)| *site == host) else {
        return OTHER_ROUTE;
    };
    let path = fold(&clean(&percent_decode(
        uri.split('?').next().unwrap_or_default(),
    )));
    routes
        .iter()
        .copied()
        .find(|route| match route.strip_suffix('*') {
            Some(prefix) => path.starts_with(prefix),
            None => path == *route,
        })
        .unwrap_or(OTHER_ROUTE)
}

/// `%XX` to the byte it names; a `%` not followed by two hex digits stays
/// literal, as it does in a request Caddy answers at all.
fn percent_decode(raw: &str) -> String {
    let bytes = raw.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        let decoded = (bytes[i] == b'%' && i + 2 < bytes.len())
            .then(|| std::str::from_utf8(&bytes[i + 1..i + 3]).ok())
            .flatten()
            .and_then(|hex| u8::from_str_radix(hex, 16).ok());
        match decoded {
            Some(byte) => {
                out.push(byte);
                i += 3;
            }
            None => {
                out.push(bytes[i]);
                i += 1;
            }
        }
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// Go's `path.Clean` with the trailing slash kept, which is Caddy's
/// `CleanPath` with slashes merged: `.` segments drop, `..` pops, doubled
/// slashes collapse, and the result is rooted.
fn clean(path: &str) -> String {
    let mut segments: Vec<&str> = Vec::new();
    for segment in path.split('/') {
        match segment {
            "" | "." => {}
            ".." => {
                segments.pop();
            }
            other => segments.push(other),
        }
    }
    let mut cleaned = String::with_capacity(path.len() + 1);
    for segment in &segments {
        cleaned.push('/');
        cleaned.push_str(segment);
    }
    if cleaned.is_empty() || path.ends_with('/') {
        cleaned.push('/');
    }
    cleaned
}

#[cfg(test)]
mod tests {
    use std::collections::{BTreeMap, BTreeSet};

    use super::*;

    const API: &str = "api.muchq.com";

    #[test]
    fn exact_matchers_take_the_path_and_nothing_under_it() {
        assert_eq!(route_of(API, "/portrait/v1/trace"), "/portrait/v1/trace");
        assert_eq!(
            route_of(API, "/portrait/v1/trace?x=1"),
            "/portrait/v1/trace"
        );
        assert_eq!(route_of(API, "/PORTRAIT/V1/TRACE"), "/portrait/v1/trace");
        assert_eq!(route_of(API, "/portrait/v1/trace/"), OTHER_ROUTE);
        assert_eq!(route_of(API, "/portrait/v1/trace/extra"), OTHER_ROUTE);
        assert_eq!(route_of(API, "/portrait/v1"), OTHER_ROUTE);
    }

    #[test]
    fn prefix_matchers_take_everything_under_the_slash() {
        assert_eq!(route_of(API, "/metrics/v1/"), "/metrics/v1/*");
        assert_eq!(
            route_of(API, "/metrics/v1/services/cpu?range=1h"),
            "/metrics/v1/*"
        );
        // Caddy's `/x/*` needs the slash: `/x` itself is unrouted.
        assert_eq!(route_of(API, "/metrics/v1"), OTHER_ROUTE);
        assert_eq!(route_of(API, "/metrics/v10/x"), OTHER_ROUTE);
    }

    #[test]
    fn exact_and_prefix_matchers_on_one_site_are_disjoint() {
        assert_eq!(route_of(API, "/1d4/v1/index"), "/1d4/v1/index");
        assert_eq!(route_of(API, "/1d4/v1/index/abc"), "/1d4/v1/index/*");
        assert_eq!(route_of(API, "/iili/v1/r/abc"), "/iili/v1/r/*");
        assert_eq!(route_of("i.iili.uk", "/r/abc"), "/r/*");
    }

    // The two overlapping matchers in the file live on different sites, and
    // each site sees only its own.
    #[test]
    fn a_matcher_belongs_to_its_site() {
        assert_eq!(route_of(API, "/stats/v1/one_d4/queries"), "/stats/v1/*");
        assert_eq!(
            route_of("api.1d4.net", "/stats/v1/one_d4/queries"),
            "/stats/v1/one_d4/*"
        );
        assert_eq!(route_of("api.1d4.net", "/stats/v1/summary"), OTHER_ROUTE);
        assert_eq!(route_of("i.iili.uk", "/iili/v1/r/abc"), OTHER_ROUTE);
        assert_eq!(
            route_of("git.muchq.com", "/muchq/MoonBase/src/branch/main"),
            OTHER_ROUTE
        );
        assert_eq!(route_of("unknown.example", "/mcp"), OTHER_ROUTE);
    }

    #[test]
    fn the_host_loses_its_port_and_case() {
        assert_eq!(route_of("MCP.1d4.net", "/mcp"), "/mcp");
        assert_eq!(route_of("mcp.1d4.net:443", "/mcp"), "/mcp");
        assert_eq!(route_of("", "/mcp"), OTHER_ROUTE);
    }

    // Caddy matches the decoded, cleaned path, so the spellings a scanner
    // uses to slip past a matcher land on the route Caddy actually served.
    #[test]
    fn the_path_is_matched_as_caddy_matches_it() {
        assert_eq!(route_of("mcp.1d4.net", "/%6Dcp"), "/mcp");
        assert_eq!(route_of("mcp.1d4.net", "/%6dcp"), "/mcp");
        assert_eq!(route_of("mcp.1d4.net", "//mcp"), "/mcp");
        assert_eq!(route_of("mcp.1d4.net", "/./mcp"), "/mcp");
        assert_eq!(route_of("mcp.1d4.net", "/x/../mcp"), "/mcp");
        assert_eq!(route_of("mcp.1d4.net", "/../mcp"), "/mcp");
        assert_eq!(route_of(API, "/metrics/v1/../v1/"), "/metrics/v1/*");
        // Linux Caddy keeps trailing dots and spaces; Windows would trim them.
        assert_eq!(route_of("mcp.1d4.net", "/mcp."), OTHER_ROUTE);
        assert_eq!(route_of("mcp.1d4.net", "/mcp%2e"), OTHER_ROUTE);
        // A stray percent stays literal rather than eating its neighbours.
        assert_eq!(route_of("mcp.1d4.net", "/mc%p"), OTHER_ROUTE);
        assert_eq!(route_of("mcp.1d4.net", "/mcp%"), OTHER_ROUTE);
        assert_eq!(route_of("mcp.1d4.net", "/mcp%4"), OTHER_ROUTE);
    }

    #[test]
    fn clean_is_go_path_clean_with_the_trailing_slash_kept() {
        for (raw, want) in [
            ("/", "/"),
            ("", "/"),
            ("//", "/"),
            ("/a//b", "/a/b"),
            ("/a/./b/", "/a/b/"),
            ("/a/b/../c", "/a/c"),
            ("/../../a", "/a"),
            ("/a/..", "/"),
            ("/a/../", "/"),
            ("a/b", "/a/b"),
        ] {
            assert_eq!(clean(raw), want, "clean({raw:?})");
        }
    }

    #[test]
    fn percent_decoding_is_bytewise_and_lossy_on_bad_utf8() {
        assert_eq!(percent_decode("/%6D%63p"), "/mcp");
        assert_eq!(percent_decode("/a%2Fb"), "/a/b");
        assert_eq!(percent_decode("/%"), "/%");
        assert_eq!(percent_decode("/%4"), "/%4");
        assert_eq!(percent_decode("/%zz"), "/%zz");
        assert_eq!(percent_decode("/%FF"), "/\u{FFFD}");
        assert_eq!(percent_decode("/caf%C3%A9"), "/café");
    }

    #[test]
    fn unrouted_paths_share_one_token() {
        for uri in ["/", "", "/.env", "/wp-login.php", "/r", "?x=1"] {
            assert_eq!(route_of(API, uri), OTHER_ROUTE, "{uri:?}");
        }
    }

    // Read at compile time through the rust_test's compile_data, so a `path`
    // matcher added to a site without a line here fails this crate's tests,
    // and one removed does too. Only `path` matchers: a `not path` is a
    // refusal's exclusion, and `path_regexp` and bare handles route nothing
    // this vocabulary names.
    const CADDYFILE: &str = include_str!("../../../../../deploy/consolidated/Caddyfile");

    fn caddyfile_path_matchers() -> BTreeMap<&'static str, BTreeSet<&'static str>> {
        let mut by_site = BTreeMap::new();
        let mut site = None;
        for line in CADDYFILE.lines() {
            // A site block opens at column zero with its address; a snippet
            // opens with a parenthesised name and is not a site.
            if !line.starts_with(['\t', ' ', '#', '(', '}']) && line.ends_with(" {") {
                site = Some(line.trim_end_matches(" {"));
                continue;
            }
            if line.starts_with('}') {
                site = None;
                continue;
            }
            let mut words = line.split_whitespace();
            if words.next() != Some("path") {
                continue;
            }
            let site = site.expect("a path matcher outside every site block");
            by_site
                .entry(site)
                .or_insert_with(BTreeSet::new)
                .extend(words);
        }
        by_site
    }

    #[test]
    fn routes_are_exactly_the_caddyfile_path_matchers_by_site() {
        let in_caddyfile = caddyfile_path_matchers();
        let matchers: usize = in_caddyfile.values().map(BTreeSet::len).sum();
        assert!(matchers > 10, "parsed too few matchers: {in_caddyfile:?}");
        let here: BTreeMap<&str, BTreeSet<&str>> = ROUTES
            .iter()
            .map(|(site, routes)| (*site, routes.iter().copied().collect()))
            .collect();
        assert_eq!(
            here, in_caddyfile,
            "ROUTES and the Caddyfile's path matchers differ"
        );
        for (site, routes) in ROUTES {
            assert_eq!(here[site].len(), routes.len(), "{site} repeats a matcher");
        }
    }

    // Also the guard on "no site's matchers overlap": a matcher shadowed by
    // an earlier prefix on its site would answer as that prefix here.
    #[test]
    fn every_route_claims_its_own_matcher_shape() {
        for (site, routes) in ROUTES {
            for route in *routes {
                let probe = route
                    .strip_suffix('*')
                    .map(|p| format!("{p}x"))
                    .unwrap_or_else(|| route.to_string());
                assert_eq!(route_of(site, &probe), *route, "{site} {probe:?}");
                assert_ne!(*route, OTHER_ROUTE);
            }
        }
    }
}
