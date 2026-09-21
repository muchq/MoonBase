use crate::classify::fold;

/// Every `path` matcher in `deploy/consolidated/Caddyfile`, by site, spelled
/// as the Caddyfile spells it. A route token is one of a site's matchers or
/// `OTHER_ROUTE`, so the vocabulary is the gateway's `path` matchers and
/// nothing else: a site routed by `path_regexp` or a bare `handle`, which is
/// all of git.muchq.com, is route-blind on purpose, and its status and probe
/// factors carry what signal there is. The test below pins this table equal
/// to the Caddyfile in both directions. A trailing `/*` is a prefix matcher,
/// anything else is exact. No two entries overlap, so the first match is the
/// only one; the shape test below is what says so. Where the Caddyfile splits
/// a path out of a prefix to route it by method — `POST /deja/v1/next` under
/// `GET /deja/v1/*` — the table keeps the prefix that claims the path, since
/// the method is already a factor of its own.
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
            "/deja/v1/*",
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

/// Every site block in `deploy/consolidated/Caddyfile`, folded: the bounded
/// host vocabulary, since a request's `host` is whatever the client sent
/// and only these addresses are ones Caddy serves a site for. Pinned to
/// the Caddyfile by the test below, like `ROUTES`.
pub const SITES: &[&str] = &[
    "api.1d4.net",
    "api.muchq.com",
    "consolidated.cmptr.info",
    "git.muchq.com",
    "gpt.muchq.com",
    "i.iili.uk",
    "mcp.1d4.net",
];

/// The host of a request nothing in the Caddyfile serves: one token for
/// every address a scanner spells into the Host header.
pub const OTHER_SITE: &str = "other";

/// The Caddyfile site a request's host names, port and case dropped, or
/// `OTHER_SITE`.
pub fn site_of(host: &str) -> &'static str {
    // The port, the case and the FQDN's trailing dot are the client's to
    // vary; Caddy's host matcher ignores all three, so the same vhost is
    // reached under any of them and the column has to fold them all.
    let folded = fold(host.split(':').next().unwrap_or_default());
    let host = folded.strip_suffix('.').unwrap_or(&folded);
    SITES
        .iter()
        .copied()
        .find(|site| *site == host)
        .unwrap_or(OTHER_SITE)
}

/// The route of a request no matcher claims on its site: a scanner's path,
/// a typo, a Forgejo page, a site with no matchers. One token for all of
/// them, so the unrouted tail cannot mint a vocabulary entry per path.
pub const OTHER_ROUTE: &str = "other";

/// The Caddyfile matcher that claims `uri` on `host`, or `OTHER_ROUTE`. The
/// path is matched the way Caddy's `path` matcher matches it: percent
/// escapes decoded, `.` and `..` segments and doubled slashes cleaned, a
/// trailing slash kept, then compared case-insensitively. The host loses
/// any port, its case and any trailing dot.
pub fn route_of(host: &str, uri: &str) -> &'static str {
    let site = site_of(host);
    let Some((_, routes)) = ROUTES.iter().find(|(candidate, _)| *candidate == site) else {
        return OTHER_ROUTE;
    };
    let Some(target) = match_target(uri) else {
        return OTHER_ROUTE;
    };
    let path = fold(&target);
    routes
        .iter()
        .copied()
        .find(|route| match route.strip_suffix('*') {
            Some(prefix) => path.starts_with(prefix),
            None => path == *route,
        })
        .unwrap_or(OTHER_ROUTE)
}

/// What the client asked for: the request target's path with its percent
/// escapes decoded, case left alone. Probe classification reads this rather
/// than the cleaned path below, because a scanner's dot segments are the
/// evidence — cleaning them away is precisely what destroys the traversal
/// family's signal — while an escape is only a spelling, so `/%2Eenv` has
/// to land in the same family as `/.env`.
pub(crate) fn decoded_target(uri: &str) -> Option<String> {
    origin_path(uri).map(percent_decode)
}

/// The path Caddy's matcher compares against: `decoded_target` with its dot
/// segments and doubled slashes cleaned.
pub(crate) fn match_target(uri: &str) -> Option<String> {
    decoded_target(uri).map(|target| clean(&target))
}

/// The path of a request target as Caddy's matcher sees it, query string
/// dropped: origin-form (`/a/b`) as is, absolute-form (`https://host/a/b`)
/// from the first slash after the authority, and nothing for the asterisk
/// and authority forms, or an absolute-form with no path, which carry no
/// path for a matcher to claim.
fn origin_path(target: &str) -> Option<&str> {
    let target = target.split('?').next().unwrap_or_default();
    if target.starts_with('/') {
        return Some(target);
    }
    let (_, after_scheme) = target.split_once("://")?;
    after_scheme.find('/').map(|slash| &after_scheme[slash..])
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

    // The behavioral pin shared with stats' route_test.go: the corpus
    // replayed through both, so a log line names the same backend
    // whichever language reads it. The vocabulary half is in otel_contract.
    const ROUTES_CORPUS: &str = include_str!("../testdata/routes.tsv");

    #[test]
    fn routes_corpus_lands_every_line_where_stats_does() {
        let mut claimed = BTreeSet::new();
        let rows: Vec<Vec<&str>> = ROUTES_CORPUS
            .lines()
            .filter(|line| !line.is_empty() && !line.starts_with('#'))
            .map(|line| line.split('\t').collect())
            .collect();
        assert!(rows.len() > 10, "corpus too small to mean anything");
        for row in &rows {
            assert_eq!(row.len(), 3, "malformed corpus row {row:?}");
            let (host, uri, route) = (row[0], row[1], row[2]);
            assert_eq!(route_of(host, uri), route, "route_of({host:?}, {uri:?})");
            claimed.insert((site_of(host), route));
        }
        assert!(
            claimed.contains(&(OTHER_SITE, OTHER_ROUTE)),
            "corpus never reaches the unrouted token"
        );
        // The site is part of the key because two sites share a spelling:
        // covering /microgpt/v1/chat on api.muchq.com says nothing about
        // gpt.muchq.com.
        for (site, routes) in ROUTES {
            for route in *routes {
                assert!(
                    claimed.contains(&(*site, *route)),
                    "{route} on {site} has no corpus row"
                );
            }
        }
    }

    // A path the Caddyfile splits out to route by method reads as the
    // prefix that claims it; the method factor carries the split.
    #[test]
    fn a_method_split_path_reads_as_the_prefix_that_claims_it() {
        assert_eq!(route_of(API, "/deja/v1/next"), "/deja/v1/*");
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

    #[test]
    fn sites_are_the_caddyfile_addresses_and_everything_else_is_other() {
        assert_eq!(site_of("git.muchq.com"), "git.muchq.com");
        assert_eq!(site_of("GIT.muchq.com:443"), "git.muchq.com");
        assert_eq!(
            site_of("consolidated.cmptr.info"),
            "consolidated.cmptr.info"
        );
        for host in [
            "",
            "example.com",
            "muchq.com",
            "api.muchq.com.evil.example",
            "1.2.3.4",
        ] {
            assert_eq!(site_of(host), OTHER_SITE, "{host:?}");
        }
        assert!(!SITES.contains(&OTHER_SITE));
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

    // Caddy routes by the URL's path whatever form the request line took,
    // and the log carries the request-target as the client sent it.
    #[test]
    fn absolute_form_targets_route_by_their_path() {
        assert_eq!(route_of("mcp.1d4.net", "https://mcp.1d4.net/mcp"), "/mcp");
        assert_eq!(
            route_of("mcp.1d4.net", "http://mcp.1d4.net:80/mcp?x=1"),
            "/mcp"
        );
        assert_eq!(route_of("mcp.1d4.net", "HTTPS://MCP.1D4.NET/MCP"), "/mcp");
        assert_eq!(route_of("mcp.1d4.net", "https://other.example/mcp"), "/mcp");
        // No path to match: absolute-form without one, asterisk-form,
        // authority-form, and a bare word.
        assert_eq!(route_of("mcp.1d4.net", "https://mcp.1d4.net"), OTHER_ROUTE);
        assert_eq!(
            route_of("mcp.1d4.net", "https://mcp.1d4.net?x=1"),
            OTHER_ROUTE
        );
        assert_eq!(route_of("mcp.1d4.net", "*"), OTHER_ROUTE);
        assert_eq!(route_of("mcp.1d4.net", "mcp.1d4.net:443"), OTHER_ROUTE);
        assert_eq!(route_of("mcp.1d4.net", "mcp"), OTHER_ROUTE);
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

    // Every site in the Caddyfile and its `path` matchers, an empty set for
    // a site that has none.
    fn caddyfile_path_matchers() -> BTreeMap<&'static str, BTreeSet<&'static str>> {
        let mut by_site = BTreeMap::new();
        let mut site = None;
        for line in CADDYFILE.lines() {
            // A site block opens at column zero with its address; a snippet
            // opens with a parenthesised name and is not a site.
            if !line.starts_with(['\t', ' ', '#', '(', '}']) && line.ends_with(" {") {
                let address = line.trim_end_matches(" {");
                by_site.entry(address).or_insert_with(BTreeSet::new);
                site = Some(address);
                continue;
            }
            if line.starts_with('}') {
                site = None;
                continue;
            }
            // Three spellings reach the gateway. A `path` line inside a
            // matcher block; a matcher written inline, `@ws_play path
            // /games/v2/play`, which is what a block holding nothing but a
            // path collapses to; and `handle_path`, which carries its
            // paths as arguments and has no matcher at all. A parser that
            // reads only the first files the other two under the unrouted
            // token with nothing red.
            let mut words = line.split_whitespace().peekable();
            let paths: Vec<&str> = match words.next() {
                Some("path") => words.filter(|word| *word != "{").collect(),
                Some("handle_path") => words.take_while(|word| *word != "{").collect(),
                Some(first) if first.starts_with('@') && words.peek() == Some(&"path") => {
                    words.next();
                    words.filter(|word| *word != "{").collect()
                }
                _ => continue,
            };
            if paths.is_empty() {
                continue;
            }
            let site = site.expect("a path matcher outside every site block");
            by_site
                .entry(site)
                .or_insert_with(BTreeSet::new)
                .extend(paths);
        }
        by_site
    }

    /// The matcher on a site that claims `path` as a prefix, when another
    /// one does. `ROUTES` keeps the claimer alone: a shadowed entry could
    /// never be reached through `route_of`.
    fn shadowed_by<'a>(matchers: &BTreeSet<&'a str>, path: &str) -> Option<&'a str> {
        matchers
            .iter()
            .copied()
            .filter(|matcher| *matcher != path)
            .find(|matcher| match matcher.strip_suffix('*') {
                Some(prefix) => path.starts_with(prefix),
                None => false,
            })
    }

    #[test]
    fn routes_are_exactly_the_caddyfile_path_matchers_by_site() {
        let mut in_caddyfile = caddyfile_path_matchers();
        let matchers: usize = in_caddyfile.values().map(BTreeSet::len).sum();
        assert!(matchers > 10, "parsed too few matchers: {in_caddyfile:?}");
        // Every method-split path, named: one appearing without a line here
        // is the same mistake as a matcher appearing without one.
        let mut shadowed: Vec<(&str, &str, &str)> = Vec::new();
        for (site, site_matchers) in &mut in_caddyfile {
            let claimed: Vec<(&str, &str)> = site_matchers
                .iter()
                .copied()
                .filter_map(|path| shadowed_by(site_matchers, path).map(|by| (path, by)))
                .collect();
            for (path, by) in claimed {
                site_matchers.remove(path);
                shadowed.push((site, path, by));
            }
        }
        assert_eq!(
            shadowed,
            [("api.muchq.com", "/deja/v1/next", "/deja/v1/*")],
            "the Caddyfile's method-split paths"
        );
        let mut here: BTreeMap<&str, BTreeSet<&str>> = ROUTES
            .iter()
            .map(|(site, routes)| (*site, routes.iter().copied().collect()))
            .collect();
        // A site with no matcher is in SITES and not in ROUTES.
        for site in SITES {
            here.entry(site).or_default();
        }
        assert_eq!(here, in_caddyfile, "ROUTES, SITES and the Caddyfile differ");
        let sites: BTreeSet<&str> = SITES.iter().copied().collect();
        assert_eq!(
            sites,
            in_caddyfile.keys().copied().collect(),
            "SITES and the Caddyfile's sites differ"
        );
        for site in SITES {
            assert_eq!(fold(site), *site, "{site} is not folded");
        }
        // Requests are folded before matching and the table is compared as
        // spelled, so an entry that is not already folded could never match.
        for (site, routes) in ROUTES {
            assert_eq!(here[site].len(), routes.len(), "{site} repeats a matcher");
            assert_eq!(fold(site), *site, "{site} is not folded");
            for route in *routes {
                assert_eq!(fold(route), *route, "{route} is not folded");
            }
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
