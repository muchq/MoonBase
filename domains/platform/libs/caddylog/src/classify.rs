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

/// Lowercase the way Go's `strings.ToLower` does: one char to one char.
/// Rust's `str::to_lowercase` applies the full mapping, under which `İ`
/// becomes two chars and a marker straddling it stops matching; the stats
/// aggregator reads the same lines through Go, and the two must agree.
pub(crate) fn fold(s: &str) -> String {
    s.chars()
        .map(|c| c.to_lowercase().next().unwrap_or(c))
        .collect()
}

/// The lowercased path of a request target, query string dropped.
fn path_of(uri: &str) -> String {
    fold(uri.split('?').next().unwrap_or_default())
}

/// Buckets a User-Agent header and names the agent within the bucket. AI
/// scrapers and named bots are named by the marker that matched, so those
/// two name vocabularies are exactly the lists above. Anonymous bots and
/// the unclassified tail keep their product token, one run of
/// `[a-z0-9._-]` cut at 32 bytes, so a new crawler is readable before it
/// has a marker; that bounds the name's length and not its cardinality,
/// which a scanner rotating tokens can make as wide as it likes, so a
/// consumer that keys anything on those two classes' names caps them
/// itself, as the stats aggregator does. A browser-shaped generic bot
/// would be "mozilla" like every browser, so the marker it tripped names
/// it instead. Browsers are one unnamed bucket. Order matters: AI
/// scrapers self-identify with names that also match the generic bot
/// markers.
pub fn agent_of(user_agent: &str) -> (AgentClass, String) {
    let ua = fold(user_agent);
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
pub fn bounded_method(method: &str) -> &'static str {
    const KNOWN: [&str; 9] = [
        "GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH",
    ];
    KNOWN
        .iter()
        .copied()
        .find(|known| *known == method)
        .unwrap_or("CUSTOM")
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
    let path = path_of(uri);
    PROBE_MATCHERS
        .iter()
        .find(|(_, matcher)| matcher.is_match(&path))
        .map(|(name, _)| *name)
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeSet;

    use super::*;

    // The behavioral pin shared with stats' classify_test.go: the same
    // corpus replayed through both classifiers, so a request lands in the
    // same bucket whichever language reads the line. Tab-separated, `#`
    // comments, blank lines ignored.
    const AGENTS: &str = include_str!("../testdata/agents.tsv");
    const PROBES: &str = include_str!("../testdata/probes.tsv");

    fn rows(corpus: &'static str, columns: usize) -> Vec<Vec<&'static str>> {
        let rows: Vec<Vec<&str>> = corpus
            .lines()
            .filter(|line| !line.is_empty() && !line.starts_with('#'))
            .map(|line| line.split('\t').collect())
            .collect();
        assert!(rows.len() > 10, "corpus too small to mean anything");
        for row in &rows {
            assert_eq!(row.len(), columns, "malformed corpus row {row:?}");
        }
        rows
    }

    fn class_named(name: &str) -> AgentClass {
        [
            AgentClass::AiScraper,
            AgentClass::Bot,
            AgentClass::Browser,
            AgentClass::Other,
        ]
        .into_iter()
        .find(|class| class.as_str() == name)
        .unwrap_or_else(|| panic!("corpus names an unknown class {name:?}"))
    }

    #[test]
    fn agents_corpus_lands_every_line_where_stats_does() {
        let mut classes = BTreeSet::new();
        for row in rows(AGENTS, 3) {
            let (ua, class, name) = (row[0], class_named(row[1]), row[2]);
            assert_eq!(agent_of(ua), (class, name.to_string()), "agent_of({ua:?})");
            classes.insert(class.as_str());
        }
        assert_eq!(classes.len(), 4, "the corpus reaches every class");
    }

    #[test]
    fn probes_corpus_lands_every_line_where_stats_does() {
        let mut reached = BTreeSet::new();
        for row in rows(PROBES, 2) {
            let (uri, want) = (row[0], (!row[1].is_empty()).then_some(row[1]));
            let got = probe_of(uri);
            assert_eq!(got, want, "probe_of({uri:?})");
            reached.extend(got);
        }
        // Every family is reached by a case, so a pattern that stops
        // matching fails here rather than silently never firing again.
        for (name, _) in PROBE_FAMILIES {
            assert!(
                reached.contains(name),
                "no corpus row reaches the {name:?} family"
            );
        }
    }

    #[test]
    fn every_marker_names_itself() {
        // So the agent vocabulary for the two marker classes is exactly the
        // lists and cannot drift from them; a misordered list fails here.
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

    // Go lowercases İ (U+0130) to a plain i; the full Unicode mapping
    // appends a combining dot that splits any marker it sits in.
    #[test]
    fn folding_is_go_simple_lowercasing() {
        assert_eq!(fold("PERPLEXİTYBOT"), "perplexitybot");
        assert_eq!(fold("Straße É"), "straße é");
        assert_eq!(
            agent_of("PERPLEXİTYBOT/1.0"),
            (AgentClass::AiScraper, "perplexitybot".to_string())
        );
        assert_eq!(probe_of("/WP-LOGİN.PHP"), Some("wordpress"));
    }

    #[test]
    fn class_spellings_are_the_stats_columns() {
        assert_eq!(AgentClass::AiScraper.as_str(), "ai_scraper");
        assert_eq!(AgentClass::Bot.as_str(), "bot");
        assert_eq!(AgentClass::Browser.as_str(), "browser");
        assert_eq!(AgentClass::Other.as_str(), "other");
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
}
