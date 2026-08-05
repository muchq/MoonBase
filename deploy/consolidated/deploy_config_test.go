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
	"regexp"
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
		return // Since #1307 portrait is probed — on /health, never the trace route.
	}
	for _, route := range []string{"/portrait/v1/trace", "/v1/trace"} {
		if strings.Contains(block, route) {
			t.Errorf("portrait's compose healthcheck probes %s. A render too large to allocate — "+
				"a 503 the client is meant to retry smaller — would mark the container unhealthy, "+
				"and unhealthy is what health-conditioned tooling acts on (an orchestrator "+
				"restarts on it). Probe the dedicated /health endpoint instead.", route)
		}
	}
}

// Every first-party service that reports the http_server_* family carries a
// /health probe (#1307). The probe is not only container supervision: the
// OTel SDK exports nothing for an instrument that has never recorded, and
// otelcol's Prometheus endpoint expires a dead process's series within
// minutes, so after each deploy a service with no background traffic went
// completely dark on the dashboard until its first real request. The steady
// probe is what keeps the series alive from boot. A healthcheck quietly
// deleted here would regress that without failing anything else.
var servicesWithSteadyProbes = []string{
	"golf_hub",
	"mcpserver",
	"microgpt-serve",
	"mithril",
	"one_d4",
	"portrait",
	"posterize",
}

func TestEveryServiceOnTheStandardRailsIsProbed(t *testing.T) {
	probes := healthcheckProbes(t, "compose.yaml")
	for _, service := range servicesWithSteadyProbes {
		if _, ok := probes[service]; !ok {
			t.Errorf("%s has no compose healthcheck. Without the steady probe its http_server_* "+
				"series die with every deploy and the dashboard goes blind until the first real "+
				"request (#1307); if removing it is deliberate, remove it from this list too.",
				service)
		}
	}
}

// The /health literal lives in three places that nothing structurally ties
// together: the probe request lines here, prom_proxy's probeFilter (which
// subtracts route="/health" from every Serving number, #1303), and the
// Probes tiles (which select route="/health"). A renamed probe path would
// silently un-exclude the probe from Serving while the Probes tile read a
// permanent zero — the shape prom_proxy's own comments call "a zero that
// means healthy and also broken". This pins the compose side of every
// first-party probe to the same literal the query side's
// TestRegistry_StandardServingQueriesExcludeTheProbeRoute pins.
func TestFirstPartyHealthchecksProbeTheRouteProbeFilterSubtracts(t *testing.T) {
	checked := 0
	for service, probe := range healthcheckProbes(t, "compose.yaml") {
		if !strings.Contains(probe.image, "ghcr.io/muchq/") {
			continue // Third-party containers (postgres) probe their own way.
		}
		checked++
		found := false
		for _, line := range probe.lines {
			if strings.Contains(line, "GET /health HTTP") {
				found = true
			}
		}
		if !found {
			t.Errorf("%s's healthcheck does not probe GET /health; prom_proxy subtracts and "+
				"charts exactly route=\"/health\" (#1303), so move all the literals together. "+
				"Probe lines:\n%s", service, strings.Join(probe.lines, "\n"))
		}
	}
	if checked < len(servicesWithSteadyProbes) {
		t.Fatalf("only %d first-party healthchecks parsed; the probed-services pin above expects "+
			"at least %d, so the parser has gone stale", checked, len(servicesWithSteadyProbes))
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
			// The patterns are anchored (see the test below), so compare the path part.
			pattern := strings.TrimPrefix(strings.TrimPrefix(line, "- "), "^")
			if strings.HasPrefix(pattern, hostfsMountPath+"/") {
				t.Errorf("mount-point exclude %q is prefixed with %s; filtering happens before"+
					" root_path is applied, so this can never match", pattern, hostfsMountPath)
			}
		}
	}
}

// filterset's regexp matcher is Go's MatchString, which looks for the pattern
// anywhere in the string rather than at the front. These excludes are all meant
// as path prefixes, and unanchored they overreach: `/dev/.*` drops `/mnt/dev/data`
// too. The panel still renders, just missing a volume nobody thought to exclude —
// the same shape of quiet wrongness as the /hostfs prefix above.
func TestMountPointExcludesAreAnchored(t *testing.T) {
	inExcludeBlock := false
	seen := 0
	for _, line := range activeLines(t, "o11y/otel-collector.yml") {
		switch {
		case strings.HasPrefix(line, "exclude_mount_points:"):
			inExcludeBlock = true
		case strings.HasPrefix(line, "exclude_fs_types:"), strings.HasPrefix(line, "network:"):
			inExcludeBlock = false
		case inExcludeBlock && strings.HasPrefix(line, "- "):
			seen++
			pattern := strings.TrimPrefix(line, "- ")
			if !strings.HasPrefix(pattern, "^") {
				t.Errorf("mount-point exclude %q is unanchored; filterset matches it anywhere in"+
					" the path, so it also excludes mounts that merely contain it", pattern)
			}
		}
	}
	// Without this the test passes just as well against a deleted exclude block.
	if seen == 0 {
		t.Fatal("found no mount-point excludes to check; this test is not reading the config")
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

// A healthcheck probe has to exist inside the image it runs in.
//
// The images built by //bazel/rules:oci.bzl carry their base plus one binary,
// and no base we use installs an HTTP client — there is no curl and no wget in
// any of them. A probe that shells out to either does not fail the way a sick
// container fails. It fails on the first attempt and every attempt after it,
// with
//
//	OCI runtime exec failed: exec: "curl": executable file not found in $PATH
//
// which Docker counts as a failed probe like any other. one_d4 sat permanently
// unhealthy that way while serving traffic normally, and any depends_on
// waiting on service_healthy would have blocked on it forever.
//
// Nothing catches this earlier: `docker compose config` validates the YAML, not
// the PATH, and no test in the service's own language can see the image it
// ships in. Probes here talk HTTP over bash's /dev/tcp instead — a redirection
// the shell implements itself, so it cannot be missing the way a binary can.
var binariesOurImagesLack = []*regexp.Regexp{
	regexp.MustCompile(`\bcurl\b`),
	regexp.MustCompile(`\bwget\b`),
}

func TestHealthchecksDoNotShellOutToBinariesOurImagesLack(t *testing.T) {
	probes := healthcheckProbes(t, "compose.yaml")

	// Guards the guard. Every one of these has been the state of this test at
	// some point during review: no services parsed, services parsed but no
	// healthcheck recognised, and — the one that shipped — a healthcheck found
	// whose command lines were never read.
	if len(probes) == 0 {
		t.Fatal("parsed no healthchecks out of compose.yaml; this test is reading nothing")
	}
	if _, ok := probes["one_d4"]; !ok {
		t.Fatal("no healthcheck found for one_d4, which has one; the parser has gone stale")
	}

	for service, probe := range probes {
		// Third-party images ship their own tooling — postgres has pg_isready.
		// The constraint is specific to what oci.bzl puts in an image.
		if !strings.Contains(probe.image, "ghcr.io/muchq/") {
			continue
		}
		for _, line := range probe.lines {
			for _, binary := range binariesOurImagesLack {
				if binary.MatchString(line) {
					t.Errorf("%s's healthcheck invokes %s, which is not in the image: %q\n"+
						"Every probe would fail on \"executable file not found\" while the "+
						"service is healthy. Use bash's /dev/tcp, as one_d4 does.",
						service, binary, line)
				}
			}
		}
	}
}

// A healthcheck must be readable where it is written.
//
// The guard above reads the probe command as text, so a healthcheck assembled
// from a YAML anchor elsewhere in the file is one it cannot see: the merge key
// is all that appears here, and it names no binary at all. Rather than grow a
// YAML evaluator, refuse the construct — the probes are three lines each and
// have no reason to be shared.
func TestHealthchecksAreWrittenInPlaceRatherThanMerged(t *testing.T) {
	for service, probe := range healthcheckProbes(t, "compose.yaml") {
		for _, line := range probe.lines {
			if strings.HasPrefix(line, "<<:") || strings.Contains(line, " *") {
				t.Errorf("%s's healthcheck is merged from an anchor (%q). Write it inline: the "+
					"binary guard reads these lines as text and an alias hides the command.",
					service, line)
			}
		}
	}
}

type healthcheckProbe struct {
	image string
	lines []string
}

// healthcheckProbes returns each service's healthcheck block, keyed by service.
//
// The lines are every line of the block, not just the one starting `test:`.
// Compose accepts the command as an inline list, as a block sequence, or as a
// folded scalar, and only the first keeps the command on the `test:` line —
// `docker compose config` itself normalises to the second. An earlier version
// of this read only `test:`-prefixed lines and so passed on the exact form the
// tooling emits, which is the kind of hole that makes a guard worse than none.
func healthcheckProbes(t *testing.T, name string) map[string]healthcheckProbe {
	t.Helper()
	probes := map[string]healthcheckProbe{}

	for service, block := range composeServiceLines(t, name) {
		image := ""
		var lines []string
		healthcheckIndent := -1

		for _, raw := range block {
			trimmed := strings.TrimSpace(raw)
			if trimmed == "" || strings.HasPrefix(trimmed, "#") {
				continue
			}
			indent := len(raw) - len(strings.TrimLeft(raw, " "))

			if strings.HasPrefix(trimmed, "image:") {
				image = trimmed
			}
			if trimmed == "healthcheck:" {
				healthcheckIndent = indent
				continue
			}
			if healthcheckIndent < 0 {
				continue
			}
			if indent <= healthcheckIndent {
				healthcheckIndent = -1 // The next key ends the block.
				continue
			}
			lines = append(lines, trimmed)
		}

		if len(lines) > 0 {
			probes[service] = healthcheckProbe{image: image, lines: lines}
		}
	}
	return probes
}

// composeServiceLines returns each compose service's raw lines, keyed by
// service name. Raw rather than trimmed: the healthcheck block is delimited by
// indentation, so trimming here would destroy the only thing that marks where
// it ends.
func composeServiceLines(t *testing.T, name string) map[string][]string {
	t.Helper()
	services := map[string][]string{}
	current := ""
	var block []string

	flush := func() {
		if current != "" && len(block) > 0 {
			services[current] = block
		}
		block = nil
	}

	inServices := false
	for _, line := range strings.Split(readConfig(t, name), "\n") {
		if !strings.HasPrefix(line, " ") && strings.TrimSpace(line) != "" {
			flush()
			current = ""
			inServices = strings.TrimSpace(line) == "services:"
			continue
		}
		if !inServices {
			continue
		}
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}
		// A two-space-indented key starts the next service. Comments at that
		// indentation are not keys and must not end the block.
		if !strings.HasPrefix(line, "   ") && !strings.HasPrefix(trimmed, "#") {
			flush()
			current = strings.TrimSuffix(trimmed, ":")
			continue
		}
		block = append(block, line)
	}
	flush()
	return services
}
