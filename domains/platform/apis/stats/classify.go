package stats

import (
	"regexp"
	"strings"
)

// The bounded user-agent vocabulary the stats tables key on. Four values,
// not a UA string per row: the point of classification is that "how much of
// my traffic is AI scrapers" is one GROUP BY, and an unbounded ua column is
// the same cardinality trap the metrics rails solved with route sentinels.
const (
	AgentAIScraper = "ai_scraper"
	AgentBot       = "bot"
	AgentBrowser   = "browser"
	AgentOther     = "other"
)

// Self-identified AI crawlers, matched case-insensitively as substrings.
// The list is additive and best-effort — an unlisted scraper lands in
// "bot" (if it self-identifies at all) or "browser" (if it lies), and the
// raw logs stay in S3 for reclassification when the list grows.
var aiScraperMarkers = []string{
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
}

// Bots worth a row of their own. Search engines, SEO crawlers, link
// unfurlers, internet scanners, and the HTTP libraries scanners drive —
// the marker doubles as the agent name, so the vocabulary is this list.
var namedBotMarkers = []string{
	"googlebot", "bingbot", "yandexbot", "duckduckbot", "baiduspider", "applebot",
	"ahrefsbot", "semrushbot", "mj12bot", "dotbot", "petalbot", "dataforseobot",
	"facebookexternalhit", "twitterbot", "linkedinbot", "slackbot", "discordbot",
	"telegrambot", "whatsapp", "uptimerobot",
	"censysinspect", "zgrab", "nuclei", "masscan",
	"python-requests", "go-http-client", "okhttp", "curl", "wget", "scrapy",
}

var botMarkers = []string{
	"bot", "spider", "crawl", "curl", "wget", "python-requests", "python/",
	"go-http-client", "libwww", "httpclient", "okhttp", "scrapy", "java/",
	"apache-httpclient", "phantom", "headless", "scanner", "nmap", "zgrab",
	"masscan", "nuclei", "censys",
}

// AgentClassOf buckets a User-Agent header.
func AgentClassOf(userAgent string) string {
	class, _ := AgentOf(userAgent)
	return class
}

// AgentOf buckets a User-Agent header and names the agent within the
// bucket, bounded per class: AI scrapers and named bots name themselves by
// the marker that matched, so those columns' vocabularies are exactly the
// lists above; anonymous bots and the unclassified tail keep their product
// token (one run of [a-z0-9._-], max 32 bytes) so a new crawler is readable
// before it has a marker; browsers are one unnamed bucket, because every
// browser's token is "mozilla". Order matters: AI scrapers self-identify
// with names that also match the generic bot markers.
func AgentOf(userAgent string) (class, name string) {
	ua := strings.ToLower(userAgent)
	if ua == "" {
		return AgentOther, emptyToken
	}
	for _, marker := range aiScraperMarkers {
		if strings.Contains(ua, marker) {
			return AgentAIScraper, marker
		}
	}
	for _, marker := range namedBotMarkers {
		if strings.Contains(ua, marker) {
			return AgentBot, marker
		}
	}
	for _, marker := range botMarkers {
		if strings.Contains(ua, marker) {
			return AgentBot, productToken(ua)
		}
	}
	if strings.HasPrefix(ua, "mozilla/") {
		return AgentBrowser, ""
	}
	return AgentOther, productToken(ua)
}

const (
	emptyToken     = "(empty)"
	maxTokenLength = 32
)

// productToken is the first run of [a-z0-9._-] in a lowercased UA — the
// product name of "product/version (comment)" — truncated to a bound so a
// scanner spraying UAs cannot mint wide rows, only many.
func productToken(lowerUA string) string {
	start := -1
	for i := 0; i < len(lowerUA); i++ {
		if isTokenByte(lowerUA[i]) {
			if start < 0 {
				start = i
			}
			continue
		}
		if start >= 0 {
			return clampToken(lowerUA[start:i])
		}
	}
	if start < 0 {
		return emptyToken
	}
	return clampToken(lowerUA[start:])
}

func isTokenByte(b byte) bool {
	return b >= 'a' && b <= 'z' || b >= '0' && b <= '9' || b == '.' || b == '_' || b == '-'
}

func clampToken(token string) string {
	if len(token) > maxTokenLength {
		return token[:maxTokenLength]
	}
	return token
}

// The bounded scanner-path vocabulary. A family is a shape scanners probe
// for on any host — WordPress logins, dotfiles, PHP endpoints on hosts that
// serve no PHP — and a request either matches one family or is not a
// probe. There is deliberately no "admin" family: /admin/ is a real one_d4
// route, and a family that counts real traffic is worse than none.
const (
	ProbeTraversal  = "traversal"
	ProbeWordpress  = "wordpress"
	ProbeEnv        = "env"
	ProbeGit        = "git"
	ProbeSecrets    = "secrets"
	ProbePhpmyadmin = "phpmyadmin"
	ProbePhp        = "php"
	ProbeBackup     = "backup"
	ProbeCgi        = "cgi"
	ProbeJava       = "java"
	ProbeRouter     = "router"
)

// Ordered: the first family to match wins, so the specific ones (a
// traversal through cgi-bin, phpMyAdmin's index.php) sit above the shapes
// they also match.
var probeFamilies = []struct {
	name  string
	match *regexp.Regexp
}{
	{ProbeTraversal, regexp.MustCompile(`\.\./|\.\.\\|%2e%2e|/etc/passwd`)},
	{ProbeWordpress, regexp.MustCompile(`wp-login|wp-admin|wp-content|wp-includes|wp-json|wp-config|xmlrpc\.php|wlwmanifest`)},
	{ProbeEnv, regexp.MustCompile(`/\.env`)},
	{ProbeGit, regexp.MustCompile(`/\.git(/|$)`)},
	{ProbeSecrets, regexp.MustCompile(`/\.aws/|/\.ssh/|id_rsa|\.htpasswd|\.htaccess|\.bash_history|/\.docker/`)},
	{ProbePhpmyadmin, regexp.MustCompile(`phpmyadmin|myadmin|/pma/|adminer`)},
	{ProbePhp, regexp.MustCompile(`\.php($|/)`)},
	{ProbeBackup, regexp.MustCompile(`\.(sql|bak|zip|tar|tar\.gz|tgz|rar|7z|old|orig|swp)$`)},
	{ProbeCgi, regexp.MustCompile(`/cgi-bin/`)},
	{ProbeJava, regexp.MustCompile(`/actuator|/solr/|/jenkins|/manager/html|jmx-console`)},
	{ProbeRouter, regexp.MustCompile(`/boaform/|/hnap1|/gponform/|/goform/|/tmui/`)},
}

// ProbeOf names the scanner family a request path belongs to, or "" when
// the path is not a known probe shape. Matching is on the lowercased path
// with the query string removed.
func ProbeOf(uri string) string {
	path := uri
	if q := strings.IndexByte(path, '?'); q >= 0 {
		path = path[:q]
	}
	path = strings.ToLower(path)
	for _, family := range probeFamilies {
		if family.match.MatchString(path) {
			return family.name
		}
	}
	return ""
}

// SlugOf extracts the iili short-link slug from a request, or "" when the
// request is not a redirect lookup. Two shapes reach iili: the public
// i.iili.uk/r/{slug} host and the api.muchq.com/iili/v1/r/{slug} route.
func SlugOf(host, method, uri string) string {
	if method != "GET" && method != "HEAD" {
		return ""
	}
	path := uri
	if q := strings.IndexByte(path, '?'); q >= 0 {
		path = path[:q]
	}
	var rest string
	switch {
	case strings.HasPrefix(host, "i.iili.uk") && strings.HasPrefix(path, "/r/"):
		rest = path[len("/r/"):]
	case strings.HasPrefix(path, "/iili/v1/r/"):
		rest = path[len("/iili/v1/r/"):]
	default:
		return ""
	}
	if rest == "" || strings.ContainsRune(rest, '/') || len(rest) > 64 {
		return ""
	}
	return rest
}
