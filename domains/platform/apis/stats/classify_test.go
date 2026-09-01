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
		if got, _ := AgentOf(c.ua); got != c.want {
			t.Errorf("AgentOf(%q) = %s, want %s", c.ua, got, c.want)
		}
	}
}

func TestAgentNamesAreBoundedPerClass(t *testing.T) {
	cases := []struct {
		ua        string
		wantClass string
		wantName  string
	}{
		// AI scrapers name themselves by marker, whatever else the UA says.
		{"Mozilla/5.0 AppleWebKit/537.36; compatible; GPTBot/1.2; +https://openai.com/gptbot", AgentAIScraper, "gptbot"},
		{"meta-externalagent/1.1 (+https://developers.facebook.com/docs/sharing/webmasters/crawler)", AgentAIScraper, "meta-externalagent"},
		// Named bots by marker; anonymous tooling by its product token.
		{"Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)", AgentBot, "googlebot"},
		{"Mozilla/5.0 (compatible; AhrefsBot/7.0; +http://ahrefs.com/robot/)", AgentBot, "ahrefsbot"},
		{"curl/8.6.0", AgentBot, "curl"},
		{"python-requests/2.32.0", AgentBot, "python-requests"},
		{"Go-http-client/2.0", AgentBot, "go-http-client"},
		{"my-crawler/0.1 (+https://example.com)", AgentBot, "my-crawler"},
		// Telegram quotes Twitter's marker in its own UA; the real one wins.
		{"TelegramBot (like TwitterBot)", AgentBot, "telegrambot"},
		{"Twitterbot/1.0", AgentBot, "twitterbot"},
		// A browser-shaped generic bot would be "mozilla" like every browser,
		// so the marker it tripped names it instead.
		{"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) HeadlessChrome/120.0.0.0 Safari/537.36", AgentBot, "headless"},
		{"Mozilla/5.0 (compatible; SomeNewBot/1.0)", AgentBot, "bot"},
		// Browsers are one bucket: the token would be "mozilla" for all of them.
		{"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36", AgentBrowser, ""},
		// "other" keeps its product token so the unclassified tail is readable.
		{"", AgentOther, "(empty)"},
		{"definitely-not-a-browser", AgentOther, "definitely-not-a-browser"},
		{"Weird Client 3.0", AgentOther, "weird"},
		{"<script>alert(1)</script>", AgentOther, "script"},
		{strings.Repeat("a", 200) + "/1.0", AgentOther, strings.Repeat("a", 32)},
		{"/////", AgentOther, "(empty)"},
	}
	for _, c := range cases {
		class, name := AgentOf(c.ua)
		if class != c.wantClass || name != c.wantName {
			t.Errorf("AgentOf(%q) = (%s, %q), want (%s, %q)", c.ua, class, name, c.wantClass, c.wantName)
		}
	}
	// Every marker names itself, so the agent column's vocabulary for the
	// two marker classes is exactly the lists and cannot drift from them —
	// and markerNames knows every one of them.
	for _, marker := range aiScraperMarkers {
		ua := "Mozilla/5.0 (compatible; " + strings.ToUpper(marker) + "/1.0)"
		if class, name := AgentOf(ua); class != AgentAIScraper || name != marker {
			t.Errorf("AgentOf(%q) = (%s, %q), want (%s, %q)", ua, class, name, AgentAIScraper, marker)
		}
		if !markerNames[marker] {
			t.Errorf("markerNames lacks %q", marker)
		}
	}
	for _, marker := range namedBotMarkers {
		ua := "Mozilla/5.0 (compatible; " + strings.ToUpper(marker) + "/1.0)"
		if class, name := AgentOf(ua); class != AgentBot || name != marker {
			t.Errorf("AgentOf(%q) = (%s, %q), want (%s, %q)", ua, class, name, AgentBot, marker)
		}
		if !markerNames[marker] {
			t.Errorf("markerNames lacks %q", marker)
		}
	}
	// A generic marker that a named one already covers is unreachable;
	// keeping the lists disjoint is what makes the named list the vocabulary.
	for _, marker := range botMarkers {
		if markerNames[marker] {
			t.Errorf("botMarkers repeats %q, which namedBotMarkers matches first", marker)
		}
	}
}

// The bare "bot" marker has no word boundary, so a phone brand ending in
// it reads as a bot. Known and kept: a boundary rule would also lose
// "Googlebot"-shaped names, and the AI list is consulted first regardless.
func TestBotSubstringHasNoWordBoundaryOnPurpose(t *testing.T) {
	if class, name := AgentOf("Mozilla/5.0 (Linux; Android 10; CUBOT X30) AppleWebKit/537.36 Chrome/120 Mobile Safari/537.36"); class != AgentBot || name != "bot" {
		t.Errorf("CUBOT = (%s, %q); if this changed on purpose, update the comment above", class, name)
	}
}

func TestProbeFamiliesAreBoundedAndRouteScoped(t *testing.T) {
	cases := []struct {
		uri  string
		want string
	}{
		{"/wp-login.php", ProbeWordpress},
		{"/wp-admin/", ProbeWordpress},
		{"/xmlrpc.php", ProbeWordpress},
		{"/blog/wp-includes/wlwmanifest.xml", ProbeWordpress},
		{"/.env", ProbeEnv},
		{"/.env.production?x=1", ProbeEnv},
		{"/api/.env.bak", ProbeEnv},
		{"/.envrc", ProbeEnv},
		{"/.git/config", ProbeGit},
		{"/.git/HEAD", ProbeGit},
		{"/phpmyadmin/index.php", ProbePhpmyadmin},
		{"/PMA/", ProbePhpmyadmin},
		{"/adminer.php", ProbePhpmyadmin},
		{"/vendor/phpunit/phpunit/src/Util/PHP/eval-stdin.php", ProbePhp},
		{"/index.php?s=/Index/think/app/invokefunction", ProbePhp},
		{"/.aws/credentials", ProbeSecrets},
		{"/.ssh/id_rsa", ProbeSecrets},
		{"/.htpasswd", ProbeSecrets},
		{"/backup.sql", ProbeBackup},
		{"/site.tar.gz", ProbeBackup},
		{"/db.zip", ProbeBackup},
		{"/../../etc/passwd", ProbeTraversal},
		{"/cgi-bin/%2e%2e/%2e%2e/bin/sh", ProbeTraversal},
		{"/cgi-bin/luci", ProbeCgi},
		{"/manager/html", ProbeJava},
		{"/actuator/health", ProbeJava},
		{"/solr/admin/info/system", ProbeJava},
		{"/boaform/admin/formLogin", ProbeRouter},
		{"/HNAP1/", ProbeRouter},
		{"/GponForm/diag_Form", ProbeRouter},
		{"/WP-LOGIN.PHP", ProbeWordpress}, // case-insensitive

		// Real routes on these hosts are not probes, however they are spelled.
		{"/", ""},
		{"/mcp", ""},
		{"/iili/v1/r/abc", ""},
		{"/stats/v1/summary?days=7", ""},
		{"/.well-known/acme-challenge/token", ""},
		{"/muchq/moonbase/src/branch/main/README.md", ""},
		{"/index.html", ""},
		{"/admin/reanalyze", ""}, // one_d4's real admin route; "admin" is not a family
		{"/environment", ""},
		{"/gitignore", ""},
		{"/muchq/MoonBase.git/info/refs", ""}, // an HTTP clone, not a dotdir probe
		// Forgejo serves archives and raw files with backup-looking
		// extensions, always several segments deep; backups probe the root.
		{"/muchq/MoonBase/archive/main.tar.gz", ""},
		{"/muchq/MoonBase/raw/branch/main/migrations/V004__x.sql", ""},
	}
	for _, c := range cases {
		if got := ProbeOf(c.uri); got != c.want {
			t.Errorf("ProbeOf(%q) = %q, want %q", c.uri, got, c.want)
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
		// Only api.muchq.com routes /iili/v1/r/ to iili, and only for GET;
		// anywhere else Caddy answers the path itself, so nothing was followed.
		{"gpt.muchq.com", "GET", "/iili/v1/r/anything", ""},
		{"api.muchq.com", "HEAD", "/iili/v1/r/xyz", ""},
	}
	for _, c := range cases {
		if got := SlugOf(c.host, c.method, c.uri); got != c.want {
			t.Errorf("SlugOf(%q, %s, %q) = %q, want %q", c.host, c.method, c.uri, got, c.want)
		}
	}
}
