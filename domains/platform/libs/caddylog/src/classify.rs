use std::sync::LazyLock;

use regex::Regex;

/// The bounded user-agent vocabulary: four classes, not a UA string per
/// row, so "how much of my traffic is AI scrapers" is one GROUP BY.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AgentClass {
    AiScraper,
    Bot,
    Browser,
    Other,
}

impl AgentClass {
    /// The stats tables' spelling of the class.
    pub fn as_str(self) -> &'static str {
        match self {
            AgentClass::AiScraper => "ai_scraper",
            AgentClass::Bot => "bot",
            AgentClass::Browser => "browser",
            AgentClass::Other => "other",
        }
    }
}

/// Self-identified AI crawlers, matched case-insensitively as substrings.
/// Additive and best-effort: an unlisted scraper lands in `Bot` if it
/// self-identifies at all, or `Browser` if it lies.
pub const AI_SCRAPER_MARKERS: &[&str] = &[
    "gptbot",
    "oai-searchbot",
    "chatgpt-user",
    "claudebot",
    "claude-web",
    "claude-user",
    "anthropic-ai",
    "ccbot",
    "bytespider",
    "perplexitybot",
    "perplexity-user",
    "meta-externalagent",
    "meta-externalfetcher",
    "google-extended",
    "applebot-extended",
    "amazonbot",
    "cohere-ai",
    "diffbot",
    "ai2bot",
    "omgili",
    "timpibot",
    "youbot",
];

/// Bots worth a row of their own. The marker doubles as the agent name, so
/// the vocabulary is this list. First match wins, so a marker another
/// agent's UA quotes must come after it: Telegram's UA reads
/// "TelegramBot (like TwitterBot)".
pub const NAMED_BOT_MARKERS: &[&str] = &[
    "googlebot",
    "bingbot",
    "yandexbot",
    "duckduckbot",
    "baiduspider",
    "applebot",
    "ahrefsbot",
    "semrushbot",
    "mj12bot",
    "dotbot",
    "petalbot",
    "dataforseobot",
    "facebookexternalhit",
    "telegrambot",
    "twitterbot",
    "linkedinbot",
    "slackbot",
    "discordbot",
    "whatsapp",
    "uptimerobot",
    "censysinspect",
    "zgrab",
    "nuclei",
    "masscan",
    "python-requests",
    "go-http-client",
    "okhttp",
    "curl",
    "wget",
    "scrapy",
];

/// Generic shapes that mark a bot without naming one. Anything also in
/// `NAMED_BOT_MARKERS` is matched there first and does not belong here.
pub const BOT_MARKERS: &[&str] = &[
    "bot",
    "spider",
    "crawl",
    "python/",
    "libwww",
    "httpclient",
    "java/",
    "apache-httpclient",
    "phantom",
    "headless",
    "scanner",
    "nmap",
    "censys",
];

/// Buckets a User-Agent header and names the agent within the bucket,
/// bounded per class: AI scrapers and named bots are named by the marker
/// that matched, so those vocabularies are exactly the lists above;
/// anonymous bots and the unclassified tail keep their product token (one
/// run of `[a-z0-9._-]`, max 32 bytes) so a new crawler is readable before
/// it has a marker. A browser-shaped generic bot would be "mozilla" like
/// every browser, so the marker it tripped names it instead. Browsers are
/// one unnamed bucket. Order matters: AI scrapers self-identify with names
/// that also match the generic bot markers.
pub fn agent_of(user_agent: &str) -> (AgentClass, String) {
    let ua = user_agent.to_lowercase();
    if let Some(marker) = AI_SCRAPER_MARKERS.iter().find(|m| ua.contains(*m)) {
        return (AgentClass::AiScraper, marker.to_string());
    }
    if let Some(marker) = NAMED_BOT_MARKERS.iter().find(|m| ua.contains(*m)) {
        return (AgentClass::Bot, marker.to_string());
    }
    if let Some(marker) = BOT_MARKERS.iter().find(|m| ua.contains(*m)) {
        let token = product_token(&ua);
        if token != "mozilla" {
            return (AgentClass::Bot, token);
        }
        return (AgentClass::Bot, marker.to_string());
    }
    if ua.starts_with("mozilla/") {
        return (AgentClass::Browser, String::new());
    }
    (AgentClass::Other, product_token(&ua))
}

const EMPTY_TOKEN: &str = "(empty)";
const MAX_TOKEN_LENGTH: usize = 32;

/// The first run of `[a-z0-9._-]` in a lowercased UA, the product name of
/// "product/version (comment)", truncated so a scanner spraying UAs cannot
/// mint wide rows, only many.
fn product_token(lower_ua: &str) -> String {
    let is_token_byte = |b: u8| b.is_ascii_lowercase() || b.is_ascii_digit() || b"._-".contains(&b);
    let bytes = lower_ua.as_bytes();
    let Some(start) = bytes.iter().position(|&b| is_token_byte(b)) else {
        return EMPTY_TOKEN.to_string();
    };
    let end = bytes[start..]
        .iter()
        .position(|&b| !is_token_byte(b))
        .map_or(bytes.len(), |n| start + n);
    // Token bytes are ASCII, so any cut inside the run is a char boundary.
    let token = &lower_ua[start..end.min(start + MAX_TOKEN_LENGTH)];
    token.to_string()
}

/// The nine RFC 9110 methods pass through, anything else collapses: the
/// bounding rule every metrics rail uses, because a scanner spraying
/// invented verbs must not mint a row per token.
pub fn bounded_method(method: &str) -> &str {
    const KNOWN: [&str; 9] = [
        "GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH",
    ];
    if KNOWN.contains(&method) {
        method
    } else {
        "CUSTOM"
    }
}

/// The bounded scanner-path vocabulary, ordered: the first family to match
/// wins, so the specific ones (a traversal through cgi-bin, phpMyAdmin's
/// index.php) sit above the shapes they also match. A family that also
/// matches real traffic is worse than none, which is why there is no
/// "admin" family (/admin/ is a one_d4 route) and why backup files match
/// only at the root: Forgejo serves repository archives and raw files with
/// the same extensions, several segments deep.
pub const PROBE_FAMILIES: &[(&str, &str)] = &[
    ("traversal", r"\.\./|\.\.\\|%2e%2e|/etc/passwd"),
    (
        "wordpress",
        r"wp-login|wp-admin|wp-content|wp-includes|wp-json|wp-config|xmlrpc\.php|wlwmanifest",
    ),
    ("env", r"/\.env"),
    ("git", r"/\.git(/|$)"),
    (
        "secrets",
        r"/\.aws/|/\.ssh/|id_rsa|\.htpasswd|\.htaccess|\.bash_history|/\.docker/",
    ),
    ("phpmyadmin", r"phpmyadmin|myadmin|/pma/|adminer"),
    ("php", r"\.php($|/)"),
    (
        "backup",
        r"^/[^/]+\.(sql|bak|zip|tar|tar\.gz|tgz|rar|7z|old|orig|swp)$",
    ),
    ("cgi", r"/cgi-bin/"),
    (
        "java",
        r"/actuator|/solr/|/jenkins|/manager/html|jmx-console",
    ),
    ("router", r"/boaform/|/hnap1|/gponform/|/goform/|/tmui/"),
];

static PROBE_MATCHERS: LazyLock<Vec<(&'static str, Regex)>> = LazyLock::new(|| {
    PROBE_FAMILIES
        .iter()
        .map(|(name, pattern)| (*name, Regex::new(pattern).expect("probe pattern")))
        .collect()
});

/// Names the scanner family a request path belongs to, or `None` when the
/// path is not a known probe shape. Matching is on the lowercased path with
/// the query string removed.
pub fn probe_of(uri: &str) -> Option<&'static str> {
    let path = uri.split('?').next().unwrap_or_default().to_lowercase();
    PROBE_MATCHERS
        .iter()
        .find(|(_, matcher)| matcher.is_match(&path))
        .map(|(name, _)| *name)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agent_classification_covers_the_vocabulary() {
        let cases: &[(&str, AgentClass)] = &[
            // AI scrapers win over the generic bot markers they also match.
            (
                "Mozilla/5.0 AppleWebKit/537.36; compatible; GPTBot/1.2; +https://openai.com/gptbot",
                AgentClass::AiScraper,
            ),
            (
                "Mozilla/5.0 (compatible; ClaudeBot/1.0; +claudebot@anthropic.com)",
                AgentClass::AiScraper,
            ),
            (
                "meta-externalagent/1.1 (+https://developers.facebook.com/docs/sharing/webmasters/crawler)",
                AgentClass::AiScraper,
            ),
            (
                "Bytespider; spider-feedback@bytedance.com",
                AgentClass::AiScraper,
            ),
            ("PerplexityBot/1.0", AgentClass::AiScraper),
            (
                "Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)",
                AgentClass::Bot,
            ),
            ("curl/8.6.0", AgentClass::Bot),
            ("python-requests/2.32.0", AgentClass::Bot),
            ("Go-http-client/2.0", AgentClass::Bot),
            (
                "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36",
                AgentClass::Browser,
            ),
            ("", AgentClass::Other),
            ("definitely-not-a-browser", AgentClass::Other),
        ];
        for (ua, want) in cases {
            assert_eq!(agent_of(ua).0, *want, "agent_of({ua:?})");
        }
    }

    #[test]
    fn agent_names_are_bounded_per_class() {
        let long = "a".repeat(200) + "/1.0";
        let cases: &[(&str, AgentClass, &str)] = &[
            // AI scrapers name themselves by marker, whatever else the UA says.
            (
                "Mozilla/5.0 AppleWebKit/537.36; compatible; GPTBot/1.2; +https://openai.com/gptbot",
                AgentClass::AiScraper,
                "gptbot",
            ),
            (
                "meta-externalagent/1.1 (+https://developers.facebook.com/docs/sharing/webmasters/crawler)",
                AgentClass::AiScraper,
                "meta-externalagent",
            ),
            // Named bots by marker; anonymous tooling by its product token.
            (
                "Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)",
                AgentClass::Bot,
                "googlebot",
            ),
            (
                "Mozilla/5.0 (compatible; AhrefsBot/7.0; +http://ahrefs.com/robot/)",
                AgentClass::Bot,
                "ahrefsbot",
            ),
            ("curl/8.6.0", AgentClass::Bot, "curl"),
            ("python-requests/2.32.0", AgentClass::Bot, "python-requests"),
            ("Go-http-client/2.0", AgentClass::Bot, "go-http-client"),
            (
                "my-crawler/0.1 (+https://example.com)",
                AgentClass::Bot,
                "my-crawler",
            ),
            // Telegram quotes Twitter's marker in its own UA; the real one wins.
            (
                "TelegramBot (like TwitterBot)",
                AgentClass::Bot,
                "telegrambot",
            ),
            ("Twitterbot/1.0", AgentClass::Bot, "twitterbot"),
            // A browser-shaped generic bot would be "mozilla" like every
            // browser, so the marker it tripped names it instead.
            (
                "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) HeadlessChrome/120.0.0.0 Safari/537.36",
                AgentClass::Bot,
                "headless",
            ),
            (
                "Mozilla/5.0 (compatible; SomeNewBot/1.0)",
                AgentClass::Bot,
                "bot",
            ),
            // Browsers are one bucket: the token would be "mozilla" for all of them.
            (
                "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36",
                AgentClass::Browser,
                "",
            ),
            // A browser is one whose UA opens with Mozilla; quoting it later
            // does not make a client one.
            (
                "something/1.0 (compatible; Mozilla/5.0)",
                AgentClass::Other,
                "something",
            ),
            // "other" keeps its product token so the unclassified tail is readable.
            ("", AgentClass::Other, "(empty)"),
            (
                "definitely-not-a-browser",
                AgentClass::Other,
                "definitely-not-a-browser",
            ),
            ("Weird Client 3.0", AgentClass::Other, "weird"),
            ("<script>alert(1)</script>", AgentClass::Other, "script"),
            (
                long.as_str(),
                AgentClass::Other,
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            ),
            ("/////", AgentClass::Other, "(empty)"),
        ];
        for (ua, class, name) in cases {
            assert_eq!(agent_of(ua), (*class, name.to_string()), "agent_of({ua:?})");
        }
        // Every marker names itself, so the agent vocabulary for the two
        // marker classes is exactly the lists and cannot drift from them.
        for marker in AI_SCRAPER_MARKERS {
            let ua = format!("Mozilla/5.0 (compatible; {}/1.0)", marker.to_uppercase());
            assert_eq!(
                agent_of(&ua),
                (AgentClass::AiScraper, marker.to_string()),
                "{ua}"
            );
        }
        for marker in NAMED_BOT_MARKERS {
            let ua = format!("Mozilla/5.0 (compatible; {}/1.0)", marker.to_uppercase());
            assert_eq!(agent_of(&ua), (AgentClass::Bot, marker.to_string()), "{ua}");
        }
        // A generic marker that a named one already covers is unreachable;
        // keeping the lists disjoint is what makes the named list the vocabulary.
        for marker in BOT_MARKERS {
            assert!(
                !NAMED_BOT_MARKERS.contains(marker) && !AI_SCRAPER_MARKERS.contains(marker),
                "BOT_MARKERS repeats {marker:?}, which a named list matches first"
            );
        }
    }

    // The bare "bot" marker has no word boundary, so a phone brand ending in
    // it reads as a bot. Known and kept: a boundary rule would also lose
    // "Googlebot"-shaped names, and the AI list is consulted first regardless.
    #[test]
    fn bot_substring_has_no_word_boundary_on_purpose() {
        let ua = "Mozilla/5.0 (Linux; Android 10; CUBOT X30) AppleWebKit/537.36 Chrome/120 Mobile Safari/537.36";
        assert_eq!(agent_of(ua), (AgentClass::Bot, "bot".to_string()));
    }

    #[test]
    fn methods_are_bounded_to_the_nine_verbs() {
        for verb in [
            "GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH",
        ] {
            assert_eq!(bounded_method(verb), verb);
        }
        for other in ["get", "PROPFIND", "", "GET /"] {
            assert_eq!(bounded_method(other), "CUSTOM", "{other:?}");
        }
    }

    #[test]
    fn probe_families_are_bounded_and_route_scoped() {
        let cases: &[(&str, Option<&str>)] = &[
            ("/wp-login.php", Some("wordpress")),
            ("/wp-admin/", Some("wordpress")),
            ("/xmlrpc.php", Some("wordpress")),
            ("/blog/wp-includes/wlwmanifest.xml", Some("wordpress")),
            ("/.env", Some("env")),
            ("/.env.production?x=1", Some("env")),
            ("/api/.env.bak", Some("env")),
            ("/.envrc", Some("env")),
            ("/.git/config", Some("git")),
            ("/.git/HEAD", Some("git")),
            ("/phpmyadmin/index.php", Some("phpmyadmin")),
            ("/PMA/", Some("phpmyadmin")),
            ("/adminer.php", Some("phpmyadmin")),
            (
                "/vendor/phpunit/phpunit/src/Util/PHP/eval-stdin.php",
                Some("php"),
            ),
            ("/index.php?s=/Index/think/app/invokefunction", Some("php")),
            ("/.aws/credentials", Some("secrets")),
            ("/.ssh/id_rsa", Some("secrets")),
            ("/.htpasswd", Some("secrets")),
            ("/backup.sql", Some("backup")),
            ("/site.tar.gz", Some("backup")),
            ("/db.zip", Some("backup")),
            ("/../../etc/passwd", Some("traversal")),
            ("/cgi-bin/%2e%2e/%2e%2e/bin/sh", Some("traversal")),
            ("/cgi-bin/luci", Some("cgi")),
            ("/manager/html", Some("java")),
            ("/actuator/health", Some("java")),
            ("/solr/admin/info/system", Some("java")),
            ("/boaform/admin/formLogin", Some("router")),
            ("/HNAP1/", Some("router")),
            ("/GponForm/diag_Form", Some("router")),
            ("/WP-LOGIN.PHP", Some("wordpress")), // case-insensitive
            // Real routes on these hosts are not probes, however they are spelled.
            ("/", None),
            ("/mcp", None),
            ("/iili/v1/r/abc", None),
            ("/stats/v1/summary?days=7", None),
            ("/.well-known/acme-challenge/token", None),
            ("/muchq/moonbase/src/branch/main/README.md", None),
            ("/index.html", None),
            ("/admin/reanalyze", None), // one_d4's real admin route; "admin" is not a family
            ("/environment", None),
            ("/gitignore", None),
            ("/muchq/MoonBase.git/info/refs", None), // an HTTP clone, not a dotdir probe
            // Forgejo serves archives and raw files with backup-looking
            // extensions, always several segments deep; backups probe the root.
            ("/muchq/MoonBase/archive/main.tar.gz", None),
            (
                "/muchq/MoonBase/raw/branch/main/migrations/V004__x.sql",
                None,
            ),
        ];
        let mut reached = std::collections::BTreeSet::new();
        for (uri, want) in cases {
            let got = probe_of(uri);
            assert_eq!(got, *want, "probe_of({uri:?})");
            reached.extend(got);
        }
        // Every family is reached by a case above, so a pattern that stops
        // matching is a failure here rather than a family that silently
        // never fires again.
        for (name, _) in PROBE_FAMILIES {
            assert!(
                reached.contains(name),
                "no case above reaches the {name:?} family"
            );
        }
    }
}
