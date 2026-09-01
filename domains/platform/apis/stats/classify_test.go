package stats

import (
	"strings"
	"testing"
)

func TestAgentClassificationCoversTheVocabulary(t *testing.T) {
	cases := []struct {
		ua   string
		want string
	}{
		// AI scrapers win over the generic bot markers they also match.
		{"Mozilla/5.0 AppleWebKit/537.36; compatible; GPTBot/1.2; +https://openai.com/gptbot", AgentAIScraper},
		{"Mozilla/5.0 (compatible; ClaudeBot/1.0; +claudebot@anthropic.com)", AgentAIScraper},
		{"meta-externalagent/1.1 (+https://developers.facebook.com/docs/sharing/webmasters/crawler)", AgentAIScraper},
		{"Bytespider; spider-feedback@bytedance.com", AgentAIScraper},
		{"PerplexityBot/1.0", AgentAIScraper},

		{"Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)", AgentBot},
		{"curl/8.6.0", AgentBot},
		{"python-requests/2.32.0", AgentBot},
		{"Go-http-client/2.0", AgentBot},

		{"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36", AgentBrowser},

		{"", AgentOther},
		{"definitely-not-a-browser", AgentOther},
	}
	for _, c := range cases {
		if got := AgentClassOf(c.ua); got != c.want {
			t.Errorf("AgentClassOf(%q) = %s, want %s", c.ua, got, c.want)
		}
	}
}

func TestSlugExtractionIsBoundedAndRouteScoped(t *testing.T) {
	cases := []struct {
		host, method, uri string
		want              string
	}{
		{"i.iili.uk", "GET", "/r/abc123", "abc123"},
		{"i.iili.uk", "HEAD", "/r/abc123?utm=x", "abc123"},
		{"api.muchq.com", "GET", "/iili/v1/r/xyz", "xyz"},
		// POSTs are not redirect lookups; deep paths and oversized slugs
		// are scanner shapes, not slugs.
		{"i.iili.uk", "POST", "/r/abc123", ""},
		{"i.iili.uk", "GET", "/r/a/b", ""},
		{"i.iili.uk", "GET", "/r/", ""},
		{"i.iili.uk", "GET", "/r/" + strings.Repeat("a", 100), ""},
		{"api.muchq.com", "GET", "/portrait/v1/trace", ""},
		{"git.muchq.com", "GET", "/r/abc", ""},
	}
	for _, c := range cases {
		if got := SlugOf(c.host, c.method, c.uri); got != c.want {
			t.Errorf("SlugOf(%q, %s, %q) = %q, want %q", c.host, c.method, c.uri, got, c.want)
		}
	}
}
