// Deployment invariants that portrait's error contract depends on.
//
// Portrait's Smithy model declares a 503 (RenderCapacityError) for renders it
// cannot allocate, and its handler returns 500 for anything else that fails.
// Answering 5xx on a hot route is only safe because nothing in the deployment
// treats those responses as evidence the container is sick: Caddy does no
// passive health checking, and portrait declares no container healthcheck at
// all. Neither fact is visible to any C++ test, and either is one directive
// away from turning a legitimate 503 into an outage — a passive check ejects
// the backend from Caddy's pool, a compose healthcheck on the trace route
// restarts the container. So they are pinned here rather than argued in a
// review.
//
// These are guards, not descriptions: today's configs satisfy them trivially.
// Their value is the failure they produce when someone adds the directive.

package deploy_test

import (
	"os"
	"strings"
	"testing"
)

// Caddy directives that decide a backend is unhealthy from the responses it
// gives real traffic, rather than from a dedicated probe. Any of these on a
// reverse_proxy makes portrait's 503 self-inflicted: return enough of them
// and Caddy stops routing to the container that is still perfectly able to
// serve smaller scenes.
var passiveHealthDirectives = []string{
	"fail_duration",
	"max_fails",
	"unhealthy_status",
	"unhealthy_latency",
	"unhealthy_request_count",
}

func TestCaddyDoesNoPassiveHealthChecking(t *testing.T) {
	for _, line := range directiveLines(t, "Caddyfile") {
		for _, directive := range passiveHealthDirectives {
			if firstToken(line) == directive {
				t.Errorf("Caddyfile declares passive health checking (%q): a 5xx answer from a "+
					"backend can now eject it from the pool. Portrait returns 503 for renders it "+
					"cannot allocate and 500 for unexpected failures; under this directive those "+
					"become outages rather than per-request errors.", line)
			}
		}
	}
}

// Active health checking is fine — it probes a dedicated endpoint — but only
// while it stays pointed at one. Aimed at an operation route it becomes the
// passive case wearing a probe's clothes.
func TestActiveHealthChecksNeverProbeAnOperationRoute(t *testing.T) {
	for _, line := range directiveLines(t, "Caddyfile") {
		if firstToken(line) != "health_uri" {
			continue
		}
		if strings.Contains(line, "trace") || strings.Contains(line, "/v1/") {
			t.Errorf("Caddyfile health probe targets an operation route (%q); probes belong on a "+
				"dedicated health endpoint, or a failing render reads as a dead backend.", line)
		}
	}
}

func TestPortraitHasNoHealthcheckOnTheTraceRoute(t *testing.T) {
	block := serviceBlock(t, "compose.yaml", "portrait")

	// Guards the guard: if the service is renamed or restructured, the
	// assertions below would pass by finding nothing at all.
	if !strings.Contains(block, "ghcr.io/muchq/portrait") {
		t.Fatalf("did not find portrait's image in its compose block; this test is no longer "+
			"reading the service it claims to. Block was:\n%s", block)
	}

	if !strings.Contains(block, "healthcheck") {
		return // The state this PR verified: nothing health-checks portrait.
	}
	for _, route := range []string{"/portrait/v1/trace", "/v1/trace"} {
		if strings.Contains(block, route) {
			t.Errorf("portrait's compose healthcheck probes %s. Docker restarts a container whose "+
				"healthcheck fails, so a render too large to allocate — a 503 the client is meant "+
				"to retry smaller — would bounce the service instead.", route)
		}
	}
}

// directiveLines returns the file's non-empty, non-comment lines, trimmed.
func directiveLines(t *testing.T, name string) []string {
	t.Helper()
	var lines []string
	for _, line := range strings.Split(readConfig(t, name), "\n") {
		if hash := strings.Index(line, "#"); hash >= 0 {
			line = line[:hash]
		}
		if line = strings.TrimSpace(line); line != "" {
			lines = append(lines, line)
		}
	}
	return lines
}

func firstToken(line string) string {
	fields := strings.Fields(line)
	if len(fields) == 0 {
		return ""
	}
	return fields[0]
}

// serviceBlock returns the lines of one compose service, identified by its
// two-space-indented key and continuing through every more-indented line.
func serviceBlock(t *testing.T, name, service string) string {
	t.Helper()
	var block []string
	inBlock := false
	for _, line := range strings.Split(readConfig(t, name), "\n") {
		if line == "  "+service+":" {
			inBlock = true
			continue
		}
		if !inBlock {
			continue
		}
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}
		// Any line indented two spaces or less starts the next service.
		if !strings.HasPrefix(line, "   ") {
			break
		}
		block = append(block, trimmed)
	}
	if len(block) == 0 {
		t.Fatalf("no %q service found in %s", service, name)
	}
	return strings.Join(block, "\n")
}

func readConfig(t *testing.T, name string) string {
	t.Helper()
	contents, err := os.ReadFile(name)
	if err != nil {
		t.Fatalf("reading %s: %v", name, err)
	}
	return string(contents)
}

// The host-metrics pair.
//
// The collector runs in a container, so the hostmetrics receiver reads the
// machine only through a bind mount plus a matching root_path. Those two live
// in different files, and losing either produces no error anywhere: the
// receiver happily scrapes its own namespace, cpu/memory/network keep
// reporting plausible numbers from /proc inside the container, and only the
// disk and filesystem panels go quiet — because /proc/diskstats is not the
// host's and the container's mounts are all overlay and tmpfs, which the
// scraper filters. An empty chart reads as an idle machine, so this was live
// for a while before anyone asked.
//
// Pinned as a pair rather than as two separate facts: either one alone is the
// broken state.
const hostfsMountPath = "/hostfs"

func TestHostMetricsReadsTheHostAndNotTheCollectorContainer(t *testing.T) {
	receiver := readConfig(t, "o11y/otel-collector.yml")
	compose := readConfig(t, "docker-compose.observability.yml")

	if !strings.Contains(receiver, "root_path: "+hostfsMountPath) {
		t.Errorf("hostmetrics has no root_path: %s — every scraper measures the collector container,"+
			" and the disk panels silently report nothing", hostfsMountPath)
	}
	if !strings.Contains(compose, "- /:"+hostfsMountPath+":ro") {
		t.Errorf("otelcol does not bind the host root at %s — root_path points at a path that does not"+
			" exist in the container", hostfsMountPath)
	}

	// Both scrapers are the ones that need the mount; the panels are named
	// after them. Enabled-but-unmounted was the bug, so enabled is worth
	// asserting alongside the mount rather than assumed.
	for _, scraper := range []string{"disk:", "filesystem:"} {
		if !strings.Contains(receiver, scraper) {
			t.Errorf("hostmetrics scraper %q is not enabled; the Host page charts it", scraper)
		}
	}
}
