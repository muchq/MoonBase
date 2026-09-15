//! Caddy's JSON access log, read the way `//domains/platform/apis/stats`
//! reads it: one line at a time into a bounded vocabulary. The user-agent
//! classes, bot markers and scanner-probe families are the stats service's
//! lists, pinned equal by `//domains/platform/libs/otel_contract`; the route
//! factor is the Caddyfile's own `path` matchers, pinned by this crate's test.

mod classify;
mod line;
mod route;

pub use classify::{AgentClass, agent_of, bounded_method, probe_of};
pub use line::CaddyLine;
pub use route::{OTHER_ROUTE, OTHER_SITE, ROUTES, SITES, route_of, site_of};
