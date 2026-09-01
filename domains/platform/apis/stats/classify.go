package stats

import "strings"

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

var botMarkers = []string{
	"bot", "spider", "crawl", "curl", "wget", "python-requests", "python/",
	"go-http-client", "libwww", "httpclient", "okhttp", "scrapy", "java/",
	"apache-httpclient", "phantom", "headless", "scanner", "nmap", "zgrab",
	"masscan", "nuclei", "censys",
}

// AgentClassOf buckets a User-Agent header. Order matters: AI scrapers
// self-identify with names that also match the generic bot markers.
func AgentClassOf(userAgent string) string {
	ua := strings.ToLower(userAgent)
	if ua == "" {
		return AgentOther
	}
	for _, marker := range aiScraperMarkers {
		if strings.Contains(ua, marker) {
			return AgentAIScraper
		}
	}
	for _, marker := range botMarkers {
		if strings.Contains(ua, marker) {
			return AgentBot
		}
	}
	if strings.HasPrefix(ua, "mozilla/") {
		return AgentBrowser
	}
	return AgentOther
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
