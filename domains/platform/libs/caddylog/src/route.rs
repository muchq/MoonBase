/// Every `path` matcher in `deploy/consolidated/Caddyfile`, spelled as the
/// Caddyfile spells it. A route token is one of these or `OTHER_ROUTE`, so
/// the vocabulary is the gateway's own routing table; the test below pins
/// the two equal in both directions. A trailing `/*` is a prefix matcher,
/// anything else is exact, and the longest matcher wins, so `/stats/v1/*`
/// and `/stats/v1/one_d4/*` are both routes and the deeper one takes the
/// paths it covers. The listing is per path, not per host: the token that
/// carries a route carries its host beside it.
pub const ROUTES: &[&str] = &[
    "/1d4/v1/health",
    "/1d4/v1/index",
    "/1d4/v1/index/*",
    "/1d4/v1/query",
    "/games/v2/play",
    "/games/v2/session",
    "/health",
    "/iili/v1/r/*",
    "/iili/v1/shorten",
    "/imagine/v1/blur",
    "/imagine/v1/edges",
    "/mcp",
    "/metrics/v1/*",
    "/microgpt/v1/chat",
    "/microgpt/v1/generate",
    "/mithril/v1/wordchain",
    "/portrait/v1/trace",
    "/r/*",
    "/stats/v1/*",
    "/stats/v1/one_d4/*",
    "/v1/index",
    "/v1/index/*",
    "/v1/query",
    "/v2/analyze",
];

/// The route of a request no matcher claims: a scanner's path, a typo, a
/// Forgejo page. One token for all of them, so the unrouted tail cannot
/// mint a vocabulary entry per path.
pub const OTHER_ROUTE: &str = "other";

/// The Caddyfile matcher that would claim `uri`, or `OTHER_ROUTE`. The
/// query string is dropped and the path lowercased, which is how Caddy
/// matches paths by default.
pub fn route_of(uri: &str) -> &'static str {
    let path = uri.split('?').next().unwrap_or_default().to_lowercase();
    ROUTES
        .iter()
        .copied()
        .filter(|route| match route.strip_suffix('*') {
            Some(prefix) => path.starts_with(prefix),
            None => path == *route,
        })
        .max_by_key(|route| route.len())
        .unwrap_or(OTHER_ROUTE)
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeSet;

    use super::*;

    #[test]
    fn exact_matchers_take_the_path_and_nothing_under_it() {
        assert_eq!(route_of("/portrait/v1/trace"), "/portrait/v1/trace");
        assert_eq!(route_of("/portrait/v1/trace?x=1"), "/portrait/v1/trace");
        assert_eq!(route_of("/PORTRAIT/V1/TRACE"), "/portrait/v1/trace");
        assert_eq!(route_of("/portrait/v1/trace/"), OTHER_ROUTE);
        assert_eq!(route_of("/portrait/v1/trace/extra"), OTHER_ROUTE);
        assert_eq!(route_of("/portrait/v1"), OTHER_ROUTE);
    }

    #[test]
    fn prefix_matchers_take_everything_under_the_slash() {
        assert_eq!(route_of("/metrics/v1/"), "/metrics/v1/*");
        assert_eq!(
            route_of("/metrics/v1/services/cpu?range=1h"),
            "/metrics/v1/*"
        );
        // Caddy's `/x/*` needs the slash: `/x` itself is unrouted.
        assert_eq!(route_of("/metrics/v1"), OTHER_ROUTE);
        assert_eq!(route_of("/metrics/v10/x"), OTHER_ROUTE);
    }

    #[test]
    fn the_longest_matcher_wins() {
        assert_eq!(route_of("/1d4/v1/index"), "/1d4/v1/index");
        assert_eq!(route_of("/1d4/v1/index/abc"), "/1d4/v1/index/*");
        assert_eq!(route_of("/stats/v1/summary"), "/stats/v1/*");
        assert_eq!(route_of("/stats/v1/one_d4/queries"), "/stats/v1/one_d4/*");
        assert_eq!(route_of("/r/abc"), "/r/*");
        assert_eq!(route_of("/iili/v1/r/abc"), "/iili/v1/r/*");
    }

    #[test]
    fn unrouted_paths_share_one_token() {
        for uri in [
            "/",
            "",
            "/.env",
            "/wp-login.php",
            "/muchq/MoonBase/src/branch/main",
            "/r",
            "?x=1",
        ] {
            assert_eq!(route_of(uri), OTHER_ROUTE, "{uri:?}");
        }
    }

    // Read at compile time through the rust_test's compile_data, so a route
    // added to the gateway without a line here fails this crate's tests,
    // and a route removed from the gateway does too.
    const CADDYFILE: &str = include_str!("../../../../../deploy/consolidated/Caddyfile");

    fn caddyfile_path_matchers() -> BTreeSet<&'static str> {
        CADDYFILE
            .lines()
            .map(str::trim)
            .filter(|line| !line.starts_with('#'))
            .filter_map(|line| {
                line.strip_prefix("not path ")
                    .or_else(|| line.strip_prefix("path "))
            })
            .flat_map(str::split_whitespace)
            .collect()
    }

    #[test]
    fn routes_are_exactly_the_caddyfile_path_matchers() {
        let in_caddyfile = caddyfile_path_matchers();
        assert!(
            in_caddyfile.len() > 10,
            "parsed too few matchers: {in_caddyfile:?}"
        );
        let here: BTreeSet<&str> = ROUTES.iter().copied().collect();
        assert_eq!(
            here, in_caddyfile,
            "ROUTES and the Caddyfile's path matchers differ"
        );
        assert_eq!(here.len(), ROUTES.len(), "ROUTES repeats a matcher");
    }

    #[test]
    fn every_route_claims_its_own_matcher_shape() {
        for route in ROUTES {
            let probe = route
                .strip_suffix('*')
                .map(|p| format!("{p}x"))
                .unwrap_or_else(|| route.to_string());
            assert_eq!(route_of(&probe), *route, "{probe:?}");
        }
        assert!(!ROUTES.contains(&OTHER_ROUTE));
    }
}
