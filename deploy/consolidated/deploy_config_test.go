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
	receiver := activeLines(t, "o11y/otel-collector.yml")
	compose := activeLines(t, "docker-compose.observability.yml")

	// Exact lines, not substrings. `strings.Contains` would accept a commented-out
	// directive and — worse — `root_path: /hostfs-typo`, which is the very state the
	// error message below describes: a root_path naming a path the container has no
	// mount for. Both were true of the first version of this test.
	if !hasLine(receiver, "root_path: "+hostfsMountPath) {
		t.Errorf("hostmetrics has no active `root_path: %s` — every scraper measures the collector"+
			" container, and the disk panels silently report nothing", hostfsMountPath)
	}
	if !hasLine(compose, "- /:"+hostfsMountPath+":ro") {
		t.Errorf("otelcol does not bind the host root at %s — root_path points at a path that does"+
			" not exist in the container", hostfsMountPath)
	}

	// Enabled-but-unmounted was the bug, so the scrapers are asserted alongside the
	// mount rather than assumed. These two are the ones whose panels were empty.
	for _, scraper := range []string{"disk:", "filesystem:"} {
		if !hasLinePrefix(receiver, scraper) {
			t.Errorf("hostmetrics scraper %q is not enabled; the Host page charts it", scraper)
		}
	}
}

// Config lines with comments and blanks removed, so an assertion cannot be
// satisfied by a directive somebody commented out.
func activeLines(t *testing.T, name string) []string {
	t.Helper()
	var out []string
	for _, line := range strings.Split(readConfig(t, name), "\n") {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" || strings.HasPrefix(trimmed, "#") {
			continue
		}
		out = append(out, trimmed)
	}
	return out
}

func hasLine(lines []string, want string) bool {
	for _, line := range lines {
		if line == want {
			return true
		}
	}
	return false
}

// For a mapping key whose value may be inline (`disk: {}`) or nested.
func hasLinePrefix(lines []string, want string) bool {
	for _, line := range lines {
		if strings.HasPrefix(line, want) {
			return true
		}
	}
	return false
}

// The collector rejects a match_type it does not know, and the rejection is not
// local: CreateFilterSet errors out of the filesystem scraper, that fails the
// hostmetrics receiver, and hostmetrics shares a pipeline with otlp — so an
// invalid value here takes every application metric down with the host ones.
// `glob` reads like it should work and does not; filterset accepts only these
// two (internal/filter/filterset/config.go at the pinned v0.155.0).
var validFilterMatchTypes = map[string]bool{"strict": true, "regexp": true}

func TestHostMetricsFilterConfigIsOneTheCollectorAccepts(t *testing.T) {
	for _, line := range activeLines(t, "o11y/otel-collector.yml") {
		if !strings.HasPrefix(line, "match_type:") {
			continue
		}
		value := strings.TrimSpace(strings.TrimPrefix(line, "match_type:"))
		if !validFilterMatchTypes[value] {
			t.Errorf("match_type %q is not one the collector accepts (strict or regexp);"+
				" hostmetrics fails to build and takes the otlp pipeline with it", value)
		}
	}
}

// Mount-point filters run before root_path is applied, so they are written from
// the host's perspective. A /hostfs-prefixed pattern matches nothing and the
// exclusion silently does not happen — which looks like the filter working,
// because the panel still renders.
func TestMountPointExcludesAreWrittenFromTheHostsPerspective(t *testing.T) {
	inExcludeBlock := false
	for _, line := range activeLines(t, "o11y/otel-collector.yml") {
		switch {
		case strings.HasPrefix(line, "exclude_mount_points:"):
			inExcludeBlock = true
		case strings.HasPrefix(line, "exclude_fs_types:"), strings.HasPrefix(line, "network:"):
			inExcludeBlock = false
		case inExcludeBlock && strings.HasPrefix(line, "- "):
			pattern := strings.TrimPrefix(line, "- ")
			if strings.HasPrefix(pattern, hostfsMountPath+"/") {
				t.Errorf("mount-point exclude %q is prefixed with %s; filtering happens before"+
					" root_path is applied, so this can never match", pattern, hostfsMountPath)
			}
		}
	}
}

// The two tests above read the config as text, which bounds what they can find
// to mistakes someone already thought of. `match_type: glob` was not one of
// those: it passed review and passed the line-presence test of the day, and it
// would have failed the hostmetrics receiver on deploy.
//
// scripts/validate-otel-config is the check that does not need to know the
// mistake in advance — it starts the pinned collector against the real config.
// But it only runs if CI invokes it, and a workflow that stopped invoking it
// would leave every test in this file green. So the wiring is pinned too.
//
// This asserts the call exists, not that it passed; the job's own exit code is
// the real signal. What it rules out is the silent version, where the dry-run
// is dropped and nothing anywhere goes red.
func TestCIStillRunsTheCollectorDryRun(t *testing.T) {
	const script = "scripts/validate-otel-config"
	workflow := readConfig(t, "../../.github/workflows/branch.yml")

	// An exact line, not a substring. `strings.Contains` would be satisfied by a
	// commented-out step, by a path that merely starts with this one, and by the
	// script's name appearing in a comment — which is how the golden-instrument
	// test in this same PR managed to pin nothing at all.
	invocation := "run: ./" + script
	found := false
	for _, line := range strings.Split(workflow, "\n") {
		if strings.TrimSpace(line) == invocation {
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("no active `%s` step in branch.yml — the collector config is back to being"+
			" checked only as text, and the next match_type: glob ships", invocation)
	}

	// Worthless if it is not executable: CI invokes it directly rather than
	// through `bash`, so a lost +x is a job that fails for a reason nobody will
	// connect back to this file.
	info, err := os.Stat("../../" + script)
	if err != nil {
		t.Fatalf("branch.yml runs %s but it does not exist: %v", script, err)
	}
	if info.Mode()&0o111 == 0 {
		t.Errorf("%s is not executable (%v); CI runs it directly", script, info.Mode())
	}
}
