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
	"net/url"
	"os"
	"os/exec"
	"regexp"
	"strconv"
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
	"games_hub",
	"mcpserver",
	"microgpt-serve",
	"mithril",
	"one_d4",
	"one_d4_v2",
	"portrait",
	"iili",
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

// The MCP endpoint, which is the one route in this file whose correctness a
// server-side test cannot reach.
//
// Streamable HTTP is not a POST-only protocol. A client probes GET for the
// optional server->client stream and may send DELETE to end a session;
// micronaut-mcp implements neither and answers 405, which is what the transport
// spec says to do and what every client knows how to read. Scoping the proxy to
// POST does not produce a 405 — it leaves GET and DELETE falling through to
// Caddy's own handling, and an empty 200 is a body no client can parse. That is
// the "GET /mcp returns 200 with an empty body" report in #1325, and
// McpProtocolTest.getReturns405BecauseTheSseStreamIsNotImplemented cannot see
// it: the server answers 405 correctly either way, because the request never
// reaches the server.
func TestTheMcpProxyIsNotScopedToASingleMethod(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile", "mcp.1d4.net")

	matcher := ""
	for i, line := range site {
		if !strings.HasPrefix(line, "handle @") || !strings.HasSuffix(line, "{") {
			continue
		}
		body := caddyBlockAt(site, i)
		for _, inner := range body {
			if strings.HasPrefix(inner, "reverse_proxy mcpserver") {
				matcher = strings.TrimSuffix(strings.Fields(line)[1], "{")
			}
		}
	}
	if matcher == "" {
		t.Fatalf("no `handle @... { reverse_proxy mcpserver ... }` in the mcp.1d4.net block; this "+
			"test is no longer reading the route it claims to. Block was:\n%s",
			strings.Join(site, "\n"))
	}

	defined := false
	for i, line := range site {
		if line != matcher+" {" {
			continue
		}
		defined = true
		for _, inner := range caddyBlockAt(site, i) {
			if firstToken(inner) == "method" {
				t.Errorf("the matcher gating the MCP proxy restricts methods (%q). Streamable HTTP "+
					"clients send GET and DELETE to /mcp and expect the server's 405; a method "+
					"matcher stops those reaching it and Caddy answers an empty 200 instead, which "+
					"no client can parse (#1325).", inner)
			}
		}
	}
	if !defined {
		t.Fatalf("%s gates the MCP proxy but is never defined in the block, so this test read no "+
			"matcher at all. Block was:\n%s", matcher, strings.Join(site, "\n"))
	}
}

// Caddy is the only place these headers are declared, so a browser is the only
// thing that can notice them missing. 1d4.net's /mcp page calls the endpoint
// cross-origin, and a Streamable HTTP client sends MCP-Protocol-Version on every
// request after the handshake and Mcp-Session-Id whenever a server issues one.
// Dropping either from the allow-list fails the preflight, so the call never
// leaves the browser — nothing reaches the server, no server test fails, and the
// page just shows no tools.
func TestTheMcpCorsAllowListCarriesTheProtocolHeaders(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile", "mcp.1d4.net")

	allowLists := 0
	for _, line := range site {
		if !strings.HasPrefix(line, "Access-Control-Allow-Headers") {
			continue
		}
		allowLists++
		for _, header := range []string{"Mcp-Session-Id", "MCP-Protocol-Version"} {
			if !strings.Contains(line, header) {
				t.Errorf("the MCP CORS allow-list omits %s (%q). A Streamable HTTP client sends it "+
					"on every request after the handshake, so the preflight fails and 1d4.net's "+
					"/mcp page never gets a response to render.", header, line)
			}
		}
	}
	// The preflight and the actual response each carry their own copy; one of
	// them silently losing the list is exactly the state this guards.
	if allowLists < 2 {
		t.Errorf("found %d Access-Control-Allow-Headers declarations in the mcp.1d4.net block, "+
			"expected the preflight's and the response's. Block was:\n%s",
			allowLists, strings.Join(site, "\n"))
	}
}

// api.muchq.com is called from browsers on muchq.com (incl. /iili) and on
// iili.uk (the iili_web worker SPA; the localhost origin is vite dev).
// ACAO takes a single value, so the Caddyfile echoes it per allowed origin
// via named matchers — on ordinary responses and on the preflight handler
// both. An origin falling out of either list breaks that frontend's fetches
// with no server-side signal; an unconditional ACAO would hand every caller
// the wrong origin.
func TestApiCorsEchoesEachAllowedOrigin(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile", "api.muchq.com")

	// Split the preflight handler from the rest: each origin needs its echo
	// in BOTH, and a global count of two can't tell one missing from one
	// duplicated.
	var preflight []string
	rest := site
	for i, line := range site {
		if line == "handle @options {" {
			preflight = caddyBlockAt(site, i)
			rest = append(append([]string{}, site[:i]...), site[i+len(preflight)+2:]...)
			break
		}
	}
	if preflight == nil {
		t.Fatalf("no `handle @options` block in api.muchq.com; preflights fall through to "+
			"the 404 handler and every cross-origin POST dies. Block was:\n%s",
			strings.Join(site, "\n"))
	}

	origins := map[string]string{
		"@from_muchq": "https://muchq.com",
		"@from_iili":  "https://iili.uk",
		"@from_dev":   "http://localhost:5173",
	}
	count := func(lines []string, want string) int {
		n := 0
		for _, line := range lines {
			if line == want {
				n++
			}
		}
		return n
	}
	for matcher, origin := range origins {
		if count(site, matcher+" header Origin "+origin) != 1 {
			t.Errorf("no `%s header Origin %s` matcher in the api.muchq.com block; that "+
				"frontend's fetches lose CORS.", matcher, origin)
		}
		echo := "header " + matcher + ` Access-Control-Allow-Origin "` + origin + `"`
		if count(preflight, echo) != 1 {
			t.Errorf("the @options preflight handler lacks the ACAO echo for %s; the browser "+
				"never sends the POST.", origin)
		}
		if count(rest, echo) != 1 {
			t.Errorf("ordinary responses lack the ACAO echo for %s; the browser drops the "+
				"response it already received.", origin)
		}
	}

	// The allow-list is closed: an origin added to the Caddyfile must be
	// added here deliberately.
	for _, line := range site {
		if strings.HasPrefix(line, "@from_") {
			matcher := strings.Fields(line)[0]
			if _, known := origins[matcher]; !known {
				t.Errorf("unexpected origin matcher %q; add it to this test's allow-list "+
					"deliberately or remove it.", line)
			}
		}
		if strings.HasPrefix(line, "Access-Control-Allow-Origin") ||
			strings.HasPrefix(line, "header Access-Control-Allow-Origin") {
			t.Errorf("unconditional %q would override the per-origin echo for every caller.", line)
		}
	}

	for _, half := range []struct {
		name  string
		lines []string
	}{{"preflight handler", preflight}, {"response path", rest}} {
		if n := count(half.lines, "Vary Origin"); n != 1 {
			t.Errorf("found %d `Vary Origin` in the %s, want 1; a shared cache can serve one "+
				"origin's answer to another.", n, half.name)
		}
		// Content-Type in the allow-list and POST in the methods are what
		// admit the JSON POST.
		headers, methods := false, false
		for _, line := range half.lines {
			if strings.HasPrefix(line, "Access-Control-Allow-Headers") &&
				strings.Contains(line, "Content-Type") {
				headers = true
			}
			if strings.HasPrefix(line, "Access-Control-Allow-Methods") &&
				strings.Contains(line, "POST") {
				methods = true
			}
		}
		if !headers {
			t.Errorf("the %s allow-list lost Content-Type; the shorten preflight fails and "+
				"no JSON POST leaves the browser.", half.name)
		}
		if !methods {
			t.Errorf("the %s allow-methods lost POST; the shorten preflight fails.", half.name)
		}
	}
}

// caddySiteBlock returns the comment-stripped, trimmed lines inside one site
// block, excluding its own braces.
func caddySiteBlock(t *testing.T, name, host string) []string {
	t.Helper()
	lines := directiveLines(t, name)
	for i, line := range lines {
		if line == host+" {" {
			return caddyBlockAt(lines, i)
		}
	}
	t.Fatalf("no %q site block in %s", host, name)
	return nil
}

// caddyBlockAt returns the lines nested inside the block opened at lines[open],
// which must end in "{". Nested blocks are included; the closing brace is not.
func caddyBlockAt(lines []string, open int) []string {
	depth := 0
	var block []string
	for _, line := range lines[open:] {
		depth += strings.Count(line, "{") - strings.Count(line, "}")
		if depth == 0 {
			break
		}
		block = append(block, line)
	}
	return block[1:] // block[0] is the header line that opened the block.
}

// mcpserver reaches the corpus through one_d4's HTTP API rather than a database
// of its own (#1332). Two things have to hold for that to work in the
// deployment, and neither is visible to any Java test.
//
// The upstream URL is one of them: mcpserver's own default is the Compose
// service name, so a missing variable does not fail loudly — it just happens to
// work here and would not anywhere else. Pinning it in compose keeps the
// deployment's answer written down rather than inherited from a code default.
func TestMcpserverIsPointedAtOneD4(t *testing.T) {
	block := serviceBlock(t, "compose.yaml", "mcpserver")

	if !strings.Contains(block, "ghcr.io/muchq/mcpserver") {
		t.Fatalf("did not find mcpserver's image in its compose block; this test is no longer "+
			"reading the service it claims to. Block was:\n%s", block)
	}
	if !strings.Contains(block, "ONE_D4_BASE_URL=http://one-d4:8080") {
		t.Errorf("mcpserver does not name one_d4 as its upstream. Every corpus-backed MCP tool "+
			"is an HTTP call to that service (#1332); without the variable the container falls "+
			"back to a compiled-in default, which is a deployment decision living in Java. "+
			"Block was:\n%s", block)
	}

	// The regression this guards against is the one the issue was filed about: a
	// second indexer, or a direct connection to the corpus database.
	for _, forbidden := range []string{"INDEXER_DB_URL", "PG_URL", "postgresql://"} {
		if strings.Contains(block, forbidden) {
			t.Errorf("mcpserver's compose block mentions %s. It is an HTTP client of one_d4 and "+
				"must hold no corpus database access of its own — one_d4 owns validation, the "+
				"indexing lifecycle, retention, the schema and its migrations (#1332).", forbidden)
		}
	}
}

// The analyze tool no longer goes through one_d4: it is served by the C++
// one_d4_v2 service (#1389), which mcpserver reaches by a second base URL. The
// same missing-variable failure mode applies — mcpserver's compiled-in default
// happens to name this compose file's alias, so only the pin here keeps the
// deployment's answer written down. The hyphenated host is deliberate:
// java.net.URI nulls the host of an underscored authority, see below.
func TestMcpserverIsPointedAtOneD4V2(t *testing.T) {
	block := serviceBlock(t, "compose.yaml", "mcpserver")
	if !strings.Contains(block, "ONE_D4_V2_BASE_URL=http://one-d4-v2:8090") {
		t.Errorf("mcpserver does not name one_d4_v2 as its analyze upstream. The analyze tool is "+
			"an HTTP call to that service (#1389); without the variable the container falls back "+
			"to a compiled-in default, which is a deployment decision living in Java. "+
			"Block was:\n%s", block)
	}
}

// The host one of mcpserver's upstream URL variables names.
func mcpserverUpstreamHost(t *testing.T, envVar string) string {
	t.Helper()
	match := regexp.MustCompile(envVar + `=(\S+)`).
		FindStringSubmatch(serviceBlock(t, "compose.yaml", "mcpserver"))
	if match == nil {
		t.Fatalf("mcpserver's compose block sets no %s", envVar)
	}
	parsed, err := url.Parse(match[1])
	if err != nil {
		t.Fatalf("%s=%q does not parse as a URL: %v", envVar, match[1], err)
	}
	return parsed.Hostname()
}

// mcpserver is the one Java client of one_d4, and java.net.URI gives an
// authority containing an underscore a null host — java.net.http then rejects
// the URI before opening a connection. Docker's DNS and every C-based client on
// this network accept such a name, so nothing else here would notice.
//
// The host must therefore be a legal RFC 1123 name, not merely one Docker
// resolves.
func TestTheOneD4UpstreamHostIsOneJavaCanParse(t *testing.T) {
	assertJavaParseableHost(t, "ONE_D4_BASE_URL")
}

// Same client, same URI parser, same trap: the analyze upstream moved to the
// C++ one_d4_v2 service (#1389), but the caller is still mcpserver's
// java.net.http, so its host is under the same RFC 1123 constraint — which is
// exactly why the alias below exists at all.
func TestTheOneD4V2UpstreamHostIsOneJavaCanParse(t *testing.T) {
	assertJavaParseableHost(t, "ONE_D4_V2_BASE_URL")
}

func assertJavaParseableHost(t *testing.T, envVar string) {
	t.Helper()
	host := mcpserverUpstreamHost(t, envVar)
	legal := regexp.MustCompile(`^[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)*$`)
	if !legal.MatchString(host) {
		t.Errorf("%s names host %q, which is not an RFC 1123 hostname. Docker's DNS "+
			"resolves it and curl accepts it, but java.net.URI gives it a null host and "+
			"java.net.http.HttpRequest rejects the URI, so mcpserver never opens a connection "+
			"and reports every corpus tool as unreachable.", envVar, host)
	}
}

// networkAliases returns the aliases a service publishes on one network. Read
// from the indentation rather than by matching a list item anywhere in the
// block, so an unrelated `- name` under volumes or ports cannot satisfy it and
// an alias attached to the wrong network does not count.
func networkAliases(t *testing.T, service, network string) []string {
	t.Helper()
	lines := composeServiceLines(t, "compose.yaml")[service]
	if len(lines) == 0 {
		t.Fatalf("no %q service found in compose.yaml", service)
	}
	indentOf := func(s string) int { return len(s) - len(strings.TrimLeft(s, " ")) }

	var aliases []string
	networksAt, networkAt, aliasesAt := -1, -1, -1
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" || strings.HasPrefix(trimmed, "#") {
			continue
		}
		indent := indentOf(line)
		if networksAt >= 0 && indent <= networksAt {
			networksAt, networkAt, aliasesAt = -1, -1, -1
		}
		if trimmed == "networks:" {
			networksAt = indent
			continue
		}
		if networksAt < 0 {
			continue
		}
		if networkAt >= 0 && indent <= networkAt {
			networkAt, aliasesAt = -1, -1
		}
		if trimmed == network+":" {
			networkAt = indent
			continue
		}
		if networkAt < 0 {
			continue
		}
		if aliasesAt >= 0 && indent <= aliasesAt {
			aliasesAt = -1
		}
		if trimmed == "aliases:" {
			aliasesAt = indent
			continue
		}
		if aliasesAt >= 0 && strings.HasPrefix(trimmed, "- ") {
			aliases = append(aliases, strings.TrimSpace(strings.TrimPrefix(trimmed, "- ")))
		}
	}
	return aliases
}

// A legal name is only half of it: something has to answer to it. The compose
// service key stays one_d4 — renaming it would move the volume and the pin
// variable — so any other internal name has to be published as an alias on the
// network mcpserver shares with it. Without that, the URL is well-formed and
// resolves to nothing.
//
// A dotted name is an external upstream (api.1d4.net), reached through DNS
// rather than through this network, and nothing here applies to it.
func TestTheOneD4UpstreamHostIsAnAliasOneD4Publishes(t *testing.T) {
	assertUpstreamHostResolves(t, "ONE_D4_BASE_URL", "one_d4")
}

func TestTheOneD4V2UpstreamHostIsAnAliasOneD4V2Publishes(t *testing.T) {
	assertUpstreamHostResolves(t, "ONE_D4_V2_BASE_URL", "one_d4_v2")
}

func assertUpstreamHostResolves(t *testing.T, envVar, service string) {
	t.Helper()
	host := mcpserverUpstreamHost(t, envVar)
	if host == service || strings.Contains(host, ".") {
		return
	}

	aliases := networkAliases(t, service, "app_network")
	for _, alias := range aliases {
		if alias == host {
			return
		}
	}
	t.Errorf("mcpserver calls %q but %s publishes no such alias on app_network (found %v). "+
		"Docker resolves a service by its key (%s) and by the aliases listed under the "+
		"network, so %q does not resolve.", host, service, aliases, service, host)
}

// No service serves /v1/analyze; the one analyze route is /v2/analyze on
// one_d4_v2 — see the next test. A Caddy line naming the old path routes
// the public internet at a 404, and its likeliest cause is a rollback or
// copy-paste resurrecting the v1 surface without anyone deciding to.
func TestTheRetiredV1AnalyzeRouteStaysGone(t *testing.T) {
	for _, line := range directiveLines(t, "Caddyfile") {
		if strings.Contains(line, "/v1/analyze") {
			t.Errorf("Caddy routes /v1/analyze (%q), which no service serves. Analysis is "+
				"POST /v2/analyze on one_d4_v2 (#1389).", line)
		}
	}
}

// /v2/analyze is the deliberate exception: publicly routed, POST-only, straight
// to one_d4_v2 with no rewrite (the service serves the gateway path itself, so
// the route a client sees and the route the model declares are one string).
// The exposure leans on the service's own bounds — 256KB PGN cap, 4096-ply
// cap, 20 req/min per client IP — rather than anything Caddy adds; auth is
// still an open question (#1332). This test records the decision so a future
// route change is a conversation, not an accident.
// The public surface on api.muchq.com, pinned route by route: exact matcher
// directives (nothing beyond them), the verbatim upstream, and no rewrite —
// each service serves its gateway path itself, so the route a client sees
// and the route the model declares stay one string.
var publicRoutes = []struct {
	matcher    string
	directives []string
	upstream   string
}{
	// one_d4_v2 (#1389 phase 6): mcpserver reaches it directly over the
	// Compose network, but the public route is served through Caddy.
	{"@post_v2_analyze", []string{"method POST", "path /v2/analyze"}, "one_d4_v2:8090"},
	// iili (#1359): the redirect matcher is the product.
	{"@post_iili_shorten", []string{"method POST", "path /iili/v1/shorten"}, "iili:8091"},
	{"@get_iili_redirect", []string{"method GET", "path /iili/v1/r/*"}, "iili:8091"},
	// stats (#1460): read-only aggregates, GET-only on purpose.
	{"@get_stats", []string{"method GET", "path /stats/v1/*"}, "stats:8092"},
	// games_hub (#79): the session mint and the two game streams. The
	// websocket matchers carry no method — an upgrade is a GET the browser
	// makes on its own terms.
	{"@post_golf_v2_session", []string{"method POST", "path /games/v2/session"}, "games_hub:8089"},
	{"@ws_golf_v2", []string{"path /games/v2/golf/play"}, "games_hub:8089"},
	{"@ws_thoughts_v2", []string{"path /games/v2/thoughts/play"}, "games_hub:8089"},
	// The 1d4.net stats tab (#1465) reads its own service's aggregates on
	// api.1d4.net, the host whose CORS grant covers the app — only the
	// one_d4 prefix, since the rest of the stats API is muchq.com's.
	{"@get_one_d4_stats", []string{"method GET", "path /stats/v1/one_d4/*"}, "stats:8092"},
}

func TestPublicRoutesAreDeliberatelyExact(t *testing.T) {
	lines := directiveLines(t, "Caddyfile")
	for _, route := range publicRoutes {
		t.Run(route.matcher, func(t *testing.T) {
			want := map[string]bool{}
			for _, directive := range route.directives {
				want[directive] = false
			}
			inMatcher := false
			proxied := false
			for i, line := range lines {
				switch {
				case line == route.matcher+" {":
					inMatcher = true
				case inMatcher && line == "}":
					inMatcher = false
				case inMatcher:
					if _, ok := want[line]; ok {
						want[line] = true
					} else {
						t.Errorf("%s carries %q beyond its decided directives — a broader "+
							"matcher exposes more than the decision covered.", route.matcher, line)
					}
				case line == "handle "+route.matcher+" {":
					// The route is the matcher's handle block (#1468); a bare
					// `reverse_proxy @matcher` would be shadowed by the catch-all,
					// and TestGatewayHostsAnswerUnmatchedPathsWith404 rejects one.
					for _, inner := range caddyBlockAt(lines, i) {
						switch {
						case inner == "reverse_proxy "+route.upstream:
							proxied = true
						case strings.HasPrefix(inner, "reverse_proxy "):
							t.Errorf("%s goes somewhere other than %s (%q).", route.matcher, route.upstream, inner)
						case strings.HasPrefix(inner, "rewrite") || strings.HasPrefix(inner, "uri "):
							t.Errorf("Caddy rewrites %s (%q); the service serves the gateway path itself.",
								route.matcher, inner)
						}
					}
				case (strings.HasPrefix(line, "rewrite") || strings.HasPrefix(line, "uri strip_prefix")) &&
					(strings.Contains(line, route.matcher) || namesARoutePath(line, route.directives)):
					t.Errorf("Caddy rewrites %s (%q); the service serves the gateway path itself.",
						route.matcher, line)
				}
			}
			for directive, found := range want {
				if !found {
					t.Errorf("%s does not declare %q.", route.matcher, directive)
				}
			}
			if !proxied {
				t.Errorf("Caddy has no reverse_proxy for %s; the route is dead from the internet.",
					route.matcher)
			}
		})
	}
}

// i.iili.uk is the short-link redirect host (#1359): A-record to the
// consolidated box, SPA stays on Cloudflare at iili.uk. Public path is
// /r/{slug}; iili's modeled path is /iili/v1/r/{slug}, so this site
// rewrites — the deliberate exception to "gateway path == model path".
func TestIiliRedirectHostRewritesSlugPathsToIili(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile", "i.iili.uk")

	matcher := ""
	for i, line := range site {
		if !strings.HasPrefix(line, "handle @") || !strings.HasSuffix(line, "{") {
			continue
		}
		body := caddyBlockAt(site, i)
		rewrites := false
		proxies := false
		for _, inner := range body {
			if strings.HasPrefix(inner, "rewrite ") && strings.Contains(inner, "/iili/v1") {
				rewrites = true
			}
			if strings.HasPrefix(inner, "reverse_proxy iili:8091") {
				proxies = true
			}
		}
		if rewrites && proxies {
			matcher = strings.TrimSuffix(strings.Fields(line)[1], "{")
			break
		}
	}
	if matcher == "" {
		t.Fatalf("no `handle @… { rewrite …/iili/v1…; reverse_proxy iili:8091 }` in the "+
			"i.iili.uk block; short links would 404 or hit the wrong upstream. Block was:\n%s",
			strings.Join(site, "\n"))
	}

	defined := false
	for i, line := range site {
		if line != matcher+" {" {
			continue
		}
		defined = true
		want := map[string]bool{
			"method GET HEAD": false,
			"path /r/*":       false,
		}
		for _, inner := range caddyBlockAt(site, i) {
			if _, ok := want[inner]; ok {
				want[inner] = true
			}
		}
		for directive, found := range want {
			if !found {
				t.Errorf("%s does not declare %q — GET/HEAD /r/* is the public short-link "+
					"contract on i.iili.uk.", matcher, directive)
			}
		}
	}
	if !defined {
		t.Fatalf("%s gates the redirect proxy but is never defined in the block. Block was:\n%s",
			matcher, strings.Join(site, "\n"))
	}
}

// Only the r3dr_v2 database role, database and password keep the old name
// (see compose.yaml); any other r3dr in the deploy surface is the retired Go
// stack or a missed rename.
func TestNoDeployConfigNamesR3dr(t *testing.T) {
	dbIdentifier := regexp.MustCompile(`(?i)r3dr_v2`)
	files := []string{"compose.yaml", "Caddyfile", "Caddyfile.local", "deploy.sh",
		"local_deploy.sh", "initialize_host.sh"}
	for _, name := range files {
		for i, line := range strings.Split(readConfig(t, name), "\n") {
			if strings.Contains(strings.ToLower(dbIdentifier.ReplaceAllString(line, "")), "r3dr") {
				t.Errorf("%s:%d names r3dr (%q); the shortener is iili", name, i+1,
					strings.TrimSpace(line))
			}
		}
	}
}

// Whether a Caddy line mentions one of the route's path directives (glob
// suffix trimmed).
func namesARoutePath(line string, directives []string) bool {
	for _, directive := range directives {
		if path, ok := strings.CutPrefix(directive, "path "); ok {
			if strings.Contains(line, strings.TrimSuffix(path, "*")) {
				return true
			}
		}
	}
	return false
}

// Every database host this file hands a service, as service -> hosts.
//
// Two spellings of environment (one_d4's `- KEY=value` list, games_hub's
// `KEY: value` mapping), two URL shapes (JDBC, which pgjdbc parses itself, and
// libpq, which games_hub's C++ uses) and the bare `PGHOST:` form
// golf_hub_db_init uses instead of a URL. That last one is not decoration: it is
// the only database host here that is not part of a URL, and games_hub gates on
// golf_hub_db_init with service_completed_successfully, so a host it cannot
// resolve stops games_hub from starting at all.
//
// A slice per service, not one host: last-match-wins would make a second URL in
// the same service an order-dependent silent skip.
func databaseHosts(t *testing.T) map[string][]string {
	t.Helper()
	// Host is what follows any credentials, up to the port or path.
	urlPattern := regexp.MustCompile(`(?:jdbc:)?postgresql://(?:[^@\s/]*@)?([A-Za-z0-9_.-]+)`)
	pgHostPattern := regexp.MustCompile(`^PGHOST:\s*(\S+)`)

	hosts := map[string][]string{}
	for service, lines := range composeServiceLines(t, "compose.yaml") {
		for _, line := range lines {
			trimmed := strings.TrimSpace(line)
			if strings.HasPrefix(trimmed, "#") {
				continue
			}
			if match := urlPattern.FindStringSubmatch(trimmed); match != nil {
				hosts[service] = append(hosts[service], match[1])
			}
			if match := pgHostPattern.FindStringSubmatch(trimmed); match != nil {
				hosts[service] = append(hosts[service], match[1])
			}
		}
	}
	return hosts
}

// Whether host names a service in this file that is actually a Postgres
// instance, rather than merely something Docker resolves.
//
// "Resolves to nothing" is the failure this started from, but "resolves to the
// wrong container" is the easier mistake to make and produces the same symptom:
// `jdbc:postgresql://one_d4:5432/one_d4` reads perfectly symmetrical, because
// the database is also called one_d4 — and one_d4 is a real service key, so a
// resolution-only check waves it through to a connection refused on 5432.
func isPostgresService(t *testing.T, services map[string][]string, host string) bool {
	t.Helper()
	lines, ok := services[host]
	if !ok {
		return false
	}
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "#") {
			continue
		}
		if strings.Contains(trimmed, "image: postgres:") || strings.Contains(trimmed, "POSTGRES_DB=") {
			return true
		}
	}
	return false
}

// A database URL is only as good as the name in it, and the name is the half no
// container can validate at build time: compose interpolates the password, the
// driver parses the URL, and the first sign of a hostname nothing answers to is
// a service that boots and cannot reach its data.
//
// This is not hypothetical: a URL can be renamed to point at a service name
// that isn't backed by anything yet, and the container comes up unable to
// connect. The mistake is invisible in review precisely because the name reads
// correctly.
//
// A dotted name is external and reached through DNS, the same carve-out
// TestTheOneD4UpstreamHostIsAnAliasOneD4Publishes makes.
func TestEveryDatabaseUrlNamesAHostThisComposeFilePublishes(t *testing.T) {
	services := composeServiceLines(t, "compose.yaml")
	hosts := databaseHosts(t)

	// Both consumers and the provisioning job: fewer than three means the
	// scan stopped seeing something and the rest of this proves little.
	if len(hosts) < 4 {
		t.Fatalf("found database hosts for only %d services (%v); expected at least one_d4, "+
			"games_hub and golf_hub_db_init. This test is reading less than it claims.",
			len(hosts), hosts)
	}

	for service, serviceHosts := range hosts {
		for _, host := range serviceHosts {
			if strings.Contains(host, ".") {
				continue // external, reached through DNS rather than this network
			}
			if isPostgresService(t, services, host) {
				continue
			}
			if _, isService := services[host]; isService {
				t.Errorf("%s's database host %q is a service in this file, but not a Postgres "+
					"one — it declares no postgres image and no POSTGRES_DB. Docker resolves "+
					"the name, so the container starts and then cannot reach its database.",
					service, host)
				continue
			}
			owners, aliases := aliasOwnersOnAppNetwork(t, services, host)
			if len(owners) == 0 {
				t.Errorf("%s's database host %q is not a service in compose.yaml and nothing "+
					"publishes it as an alias on app_network (aliases found: %v). The container "+
					"will start and fail to reach its database.", service, host, aliases)
				continue
			}
			// Exactly one owner, and it must be Postgres. Compose lets several
			// services publish the same alias, and Docker's DNS then answers with
			// one A record per container — so a shared alias round-robins. Mixed
			// with an app container that sends half the connections to something
			// refusing 5432; shared between two Postgres services it silently
			// splits reads and writes across two different databases. Neither is
			// ever intended for a database name, and "at least one owner is
			// Postgres" would wave both through.
			if len(owners) > 1 {
				t.Errorf("%s's database host %q is published as an alias by more than one "+
					"service (%v). Docker returns an address for each, so connections "+
					"round-robin between them.", service, host, owners)
				continue
			}
			if !isPostgresService(t, services, owners[0]) {
				t.Errorf("%s's database host %q resolves, but only as an alias of %q, which is "+
					"not a Postgres service. Docker answers the name and the connection is then "+
					"refused on 5432.", service, host, owners[0])
			}
		}
	}
}

// The services publishing host as an alias on app_network, plus every alias
// seen — the second return is what makes the failure message actionable.
//
// Owners, not a bool: resolving is not the same as resolving to a database.
// Attaching the one_d4_postgres alias to one_d4 would satisfy "something answers
// to this name" while JDBC connected to the app container and got refused on
// 5432 — the same wrong-container failure the service-key path already guards
// against, arriving by the other route.
func aliasOwnersOnAppNetwork(t *testing.T, services map[string][]string, host string) ([]string, []string) {
	t.Helper()
	var owners, aliases []string
	for candidate := range services {
		for _, alias := range networkAliases(t, candidate, "app_network") {
			aliases = append(aliases, alias)
			if alias == host {
				owners = append(owners, candidate)
			}
		}
	}
	return owners, aliases
}

// one_d4's environment lines with comments stripped. serviceBlock keeps comment
// lines, and the one_d4 block has ten of them — so a regex run over it happily
// matches a setting that has been commented out. That is not a contrived case:
// commenting the URL out is the documented rollback, and it would otherwise
// leave both compose tests green while one_d4 was silently back on the
// untracked host file.
func oneD4ActiveLines(t *testing.T) []string {
	t.Helper()
	var out []string
	for _, line := range composeServiceLines(t, "compose.yaml")["one_d4"] {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" || strings.HasPrefix(trimmed, "#") {
			continue
		}
		out = append(out, trimmed)
	}
	if len(out) == 0 {
		t.Fatal("no active lines in one_d4's compose block")
	}
	return out
}

// The value of an environment setting on one_d4, in either spelling compose
// allows, or "" when absent.
func oneD4Env(t *testing.T, key string) string {
	t.Helper()
	pattern := regexp.MustCompile(`^-?\s*` + regexp.QuoteMeta(key) + `[=:]\s*(\S+)`)
	for _, line := range oneD4ActiveLines(t) {
		if match := pattern.FindStringSubmatch(line); match != nil {
			return match[1]
		}
	}
	return ""
}

// one_d4 is the only Java consumer of the shared instance — games_hub reaches it
// from C++ through libpq — so it is the only one whose URL has to be a JDBC URL
// rather than a libpq one. The two are not interchangeable: pgjdbc rejects a URL
// without the jdbc: prefix outright, and DataSourceFactory hands whatever it is
// given straight to Hikari.
func TestOneD4sDatabaseUrlIsAJdbcUrl(t *testing.T) {
	url := oneD4Env(t, "INDEXER_DB_URL")
	if url == "" {
		t.Fatal("one_d4's compose block sets no INDEXER_DB_URL. That variable is the container's " +
			"only source for the URL — there is no file rank under it and no in-memory default — " +
			"so the container would fail to start.")
	}
	if !strings.HasPrefix(url, "jdbc:postgresql://") {
		t.Errorf("INDEXER_DB_URL=%q is not a JDBC URL. games_hub's libpq form (postgresql://...) "+
			"is what this would most likely be copied from, and pgjdbc rejects it.", url)
	}
}

// The deploy-order decoupling #1419 exists for: one_d4_worker gates on the
// one_d4_migrate one-shot having finished, not on the Java service being
// healthy. Reverting the gate to one_d4 quietly restores the "Java service
// must be up first" coupling that #1418 measured as a worker error-loop, and
// nothing else would fail — the stack still comes up, just serialized again.
func TestTheWorkerGatesOnTheMigrateStepNotTheJavaService(t *testing.T) {
	deps := dependsOn(t, "one_d4_worker")
	for _, dep := range deps {
		if dep == "one_d4" {
			t.Errorf("one_d4_worker depends_on one_d4 — the worker must not wait for the Java " +
				"service, only for the schema.")
		}
	}
	if !gatesOnCompletedMigrate(t, "one_d4_worker") {
		t.Errorf("one_d4_worker does not gate on one_d4_migrate with "+
			"service_completed_successfully (depends_on: %v). Without that gate the worker "+
			"races the schema step and error-loops on missing tables (#1418).", deps)
	}
}

// The Java service gates on the same one-shot, and now needs to: since #1426 its
// boot checks the migrations were applied rather than applying them, and refuses
// to serve otherwise. Released together without the gate it crash-loops against a
// schema the one-shot has not finished writing.
func TestTheJavaServiceAlsoGatesOnTheMigrateStep(t *testing.T) {
	if !gatesOnCompletedMigrate(t, "one_d4") {
		t.Errorf("one_d4 does not gate on one_d4_migrate with "+
			"service_completed_successfully (depends_on: %v) — its boot-time schema check then "+
			"races the one-shot, and fails against the tables it has not created yet.",
			dependsOn(t, "one_d4"))
	}
}

// Whether service's depends_on carries one_d4_migrate with
// condition: service_completed_successfully (read by adjacency, since
// dependsOn deliberately strips conditions).
func gatesOnCompletedMigrate(t *testing.T, service string) bool {
	t.Helper()
	previous := ""
	for _, line := range composeServiceLines(t, "compose.yaml")[service] {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" || strings.HasPrefix(trimmed, "#") {
			continue
		}
		if previous == "one_d4_migrate:" && trimmed == "condition: service_completed_successfully" {
			return true
		}
		previous = trimmed
	}
	return false
}

// A one-shot under `restart: always` is a restart loop: the container exits 0
// and Docker brings it straight back, forever. golf_hub_db_init carries the
// same shape; this pins it for the migrate step, whose exit is load-bearing —
// service_completed_successfully never fires for a container that keeps
// restarting.
func TestTheMigrateStepIsAOneShotWithAJdbcUrl(t *testing.T) {
	lines := composeServiceLines(t, "compose.yaml")["one_d4_migrate"]
	if len(lines) == 0 {
		t.Fatal("no one_d4_migrate service in compose.yaml — the worker's depends_on gate " +
			"has nothing to wait for.")
	}
	var restart, url, username, password string
	urlPattern := regexp.MustCompile(`INDEXER_DB_URL=(\S+)`)
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "#") {
			continue
		}
		if strings.HasPrefix(trimmed, "restart:") {
			restart = strings.TrimSpace(strings.TrimPrefix(trimmed, "restart:"))
		}
		if match := urlPattern.FindStringSubmatch(trimmed); match != nil {
			url = match[1]
		}
		if strings.HasPrefix(trimmed, "- INDEXER_DB_USERNAME=") {
			username = strings.TrimPrefix(trimmed, "- INDEXER_DB_USERNAME=")
		}
		if strings.HasPrefix(trimmed, "- INDEXER_DB_PASSWORD=") {
			password = strings.TrimPrefix(trimmed, "- INDEXER_DB_PASSWORD=")
		}
	}
	if restart != `"no"` {
		t.Errorf("one_d4_migrate's restart is %q, want \"no\": a one-shot under any restart "+
			"policy loops, and service_completed_successfully never fires for it.", restart)
	}
	if !strings.HasPrefix(url, "jdbc:postgresql://") {
		t.Errorf("one_d4_migrate's INDEXER_DB_URL=%q is not a JDBC URL — same trap "+
			"TestOneD4sDatabaseUrlIsAJdbcUrl pins for the service, same driver.", url)
	}
	if !strings.Contains(url, "socketTimeout=1800") {
		t.Errorf("one_d4_migrate's INDEXER_DB_URL=%q lost socketTimeout=1800. That parameter "+
			"is load-bearing: DataSourceFactory's 150s default severs a quiet CREATE INDEX on "+
			"a populated table, and a failed one-shot under restart \"no\" gates the worker "+
			"off until the next deploy.", url)
	}
	// The same value pins TestOneD4sCredentialsAreNotInTheUrl holds the service
	// to: the exact variable names (INDEXER_DB_USER is the plausible slip), the
	// bootstrap role, and an interpolation rather than a committed literal.
	if username != "one_d4" {
		t.Errorf("one_d4_migrate's INDEXER_DB_USERNAME=%q, expected one_d4. The URL carries "+
			"no credentials, so the connection has no user without it.", username)
	}
	if password != "${ONE_D4_DB_PASSWORD}" {
		t.Errorf("one_d4_migrate's INDEXER_DB_PASSWORD=%q should interpolate "+
			"${ONE_D4_DB_PASSWORD} from the host's ~/.env. A literal here would be a "+
			"credential committed to the repo.", password)
	}
}

// pgjdbc URL-decodes query parameter values, so a password interpolated into
// ?password= is silently corrupted when the secret contains + (becomes a space)
// or %XX, and truncated at &. `openssl rand -base64` emits + routinely, so this
// breaks on a password that works everywhere else, and surfaces as "No suitable
// driver". Credentials therefore travel as their own variables, which Hikari
// hands to the driver as connection properties — see DataSourceFactory.create.
//
// Userinfo (postgresql://user:pass@host) is not an escape hatch either: it
// dodges the decoding but defeats Hikari's password masking, whose regex only
// recognises the password= query form, so the whole URL would appear in a
// connection-failure message.
func TestOneD4sCredentialsAreNotInTheUrl(t *testing.T) {
	url := oneD4Env(t, "INDEXER_DB_URL")

	if strings.Contains(url, "password=") {
		t.Errorf("INDEXER_DB_URL=%q carries the password as a query parameter. pgjdbc decodes "+
			"those, so a secret containing + or %% is silently mangled and one containing & is "+
			"truncated. Pass INDEXER_DB_PASSWORD instead.", url)
	}
	if strings.Contains(strings.TrimPrefix(url, "jdbc:postgresql://"), "@") {
		t.Errorf("INDEXER_DB_URL=%q embeds credentials in the URL's userinfo, which Hikari's "+
			"password masking does not recognise — the URL would appear in full in a "+
			"connection-failure message.", url)
	}
	// The username half. Deleting or misspelling it — INDEXER_DB_USERNAME is the
	// name one_d4's own docs use, and INDEXER_DB_USER is the plausible slip —
	// leaves a credential-free URL that DataSourceFactory pairs with no username,
	// and /health keeps answering 200 with a DOWN body.
	if username := oneD4Env(t, "INDEXER_DB_USERNAME"); username != "one_d4" {
		t.Errorf("INDEXER_DB_USERNAME=%q, expected one_d4. The URL carries no credentials, so "+
			"the connection has no user without it.", username)
	}
	if password := oneD4Env(t, "INDEXER_DB_PASSWORD"); password != "${ONE_D4_DB_PASSWORD}" {
		t.Errorf("INDEXER_DB_PASSWORD=%q should interpolate ${ONE_D4_DB_PASSWORD} from the "+
			"host's ~/.env. A literal here would be a credential committed to the repo.",
			password)
	}
}

// one_d4 reads nothing from the host filesystem: its JDBC URL comes from the
// environment above and there is no file rank under it. A mount here would be
// configuration the deploy appears to honour and does not — an operator can edit
// that file all day and change nothing, which is the kind of thing found out
// during an incident.
//
// The regression this guards is a host file coming back for one_d4 instead of a
// variable here. That shape is what keeps a database hostname invisible to this
// repo, and it is why one_d4_postgres has to survive as an alias (#1225).
func TestOneD4MountsNoHostConfigDirectory(t *testing.T) {
	lines := oneD4ActiveLines(t)

	// The control. Without it this passes just as well against a block that was
	// renamed out from under the helper, or emptied.
	if !strings.Contains(strings.Join(lines, "\n"), "ghcr.io/muchq/one_d4") {
		t.Fatalf("did not find one_d4's image in its compose block; this test is no longer "+
			"reading the service it claims to. Lines were:\n%s", strings.Join(lines, "\n"))
	}

	for _, line := range lines {
		if strings.Contains(line, "/etc/one_d4") {
			t.Errorf("one_d4's compose block mounts a host config directory: %q. Nothing in the "+
				"container reads one — the JDBC URL comes from INDEXER_DB_URL, and nothing "+
				"resolves it from a file.", line)
		}
	}
}

// No compose database URL may carry a literal credential. Scoped to the whole
// file rather than to one_d4, because "the secret comes from ~/.env" is a
// property of every URL here, not of the one this change happened to touch.
func TestNoDatabaseUrlCarriesALiteralPassword(t *testing.T) {
	pattern := regexp.MustCompile(`(?:jdbc:)?postgresql://(\S*)`)
	checked := 0
	for service, lines := range composeServiceLines(t, "compose.yaml") {
		for _, line := range lines {
			trimmed := strings.TrimSpace(line)
			if strings.HasPrefix(trimmed, "#") {
				continue
			}
			match := pattern.FindStringSubmatch(trimmed)
			if match == nil {
				continue
			}
			checked++
			rest := match[1]
			credentials := ""
			if at := strings.Index(rest, "@"); at >= 0 {
				credentials = rest[:at]
			}
			if idx := strings.Index(rest, "password="); idx >= 0 {
				credentials += rest[idx:]
			}
			if credentials != "" && !strings.Contains(credentials, "${") {
				t.Errorf("%s's database URL carries a literal credential (%q). Interpolate it "+
					"from the host's ~/.env instead.", service, credentials)
			}
		}
	}
	if checked == 0 {
		t.Fatal("no database URLs found in compose.yaml; this test checked nothing")
	}
}

const postgresService = "shared_postgres"

// Compose prefixes volume keys with the project name, so this key is not the
// volume's name — ubuntu_shared_pgdata is. Renaming the key therefore does not
// move the cluster: it mounts a fresh empty volume, initdb fills it, and the
// stack comes up healthy and blank. A `name:` here would dodge the prefix but
// hardcode one project's, so both are rejected. Moving the cluster is a host
// operation, and this test is what makes you do it there.
func TestTheSharedPostgresVolumeKeyIsPinned(t *testing.T) {
	if block := serviceBlock(t, "compose.yaml", postgresService); !strings.Contains(block, "- shared_pgdata:") {
		t.Errorf("%s no longer mounts the shared_pgdata volume. The cluster lives in that "+
			"volume under a project prefix; a renamed key mounts a fresh empty one for initdb "+
			"to fill, which deploys green and blank.", postgresService)
	}

	declared := false
	inVolumes := false
	for _, line := range strings.Split(readConfig(t, "compose.yaml"), "\n") {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" || strings.HasPrefix(trimmed, "#") {
			continue
		}
		if !strings.HasPrefix(line, " ") {
			inVolumes = trimmed == "volumes:"
			continue
		}
		if !inVolumes {
			continue
		}
		if trimmed == "shared_pgdata:" {
			declared = true
			continue
		}
		if declared && strings.HasPrefix(line, "   ") {
			t.Errorf("the shared_pgdata volume key carries an option (%q). A concrete `name:` "+
				"hardcodes one project's prefix, and the day that stops holding Docker creates "+
				"an empty volume rather than failing.", trimmed)
		}
		if !strings.HasPrefix(line, "   ") {
			declared = false
		}
	}
	if !strings.Contains(readConfig(t, "compose.yaml"), "\n  shared_pgdata:") {
		t.Errorf("no shared_pgdata key in the top-level volumes block; the mount above would " +
			"become a bind mount or fail outright.")
	}
}

// depends_on is checked by Compose, but on the host — this pins it here, where
// a rename that missed one is a failing test rather than a failed deploy.
func TestEveryDependsOnNamesAServiceThatExists(t *testing.T) {
	services := composeServiceLines(t, "compose.yaml")
	for service := range services {
		for _, dep := range dependsOn(t, service) {
			if _, ok := services[dep]; !ok {
				t.Errorf("%s depends_on %q, which is not a service in compose.yaml. Compose "+
					"refuses to start the project on this, so the rename that left it behind "+
					"fails on the deploy host instead of here.", service, dep)
			}
		}
	}
}

// dependsOn returns the services one service depends on, in either the list
// form (`- svc`) or the map form (`svc:` with a nested `condition:`). Read from
// the indentation, so a nested condition is not mistaken for a dependency.
func dependsOn(t *testing.T, service string) []string {
	t.Helper()
	indentOf := func(s string) int { return len(s) - len(strings.TrimLeft(s, " ")) }

	var deps []string
	blockAt, entryAt := -1, -1
	for _, line := range composeServiceLines(t, "compose.yaml")[service] {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" || strings.HasPrefix(trimmed, "#") {
			continue
		}
		indent := indentOf(line)
		if blockAt >= 0 && indent <= blockAt {
			blockAt, entryAt = -1, -1
		}
		if trimmed == "depends_on:" {
			blockAt = indent
			continue
		}
		if blockAt < 0 {
			continue
		}
		if entryAt < 0 {
			entryAt = indent
		}
		if indent != entryAt {
			continue // a nested `condition:`, not a dependency
		}
		deps = append(deps, strings.TrimSuffix(strings.TrimPrefix(trimmed, "- "), ":"))
	}
	return deps
}

// `deploy.sh --services` answers "what can I pass to --service", and the two
// have to agree. The listing used to be built from the com.muchq.description
// labels, which happened to name exactly the services carrying a pinned image
// — until shared_postgres got a description and no image (#1225). That made
// the listing offer a target --service rejects, and turned the old
// count-mismatch note into a permanent, false "some services have no
// description" warning.
//
// This runs the script rather than reading it: the listing is awk and sed, and
// a text assertion about awk is not an assertion about its output.
func TestTheServicesListingMatchesWhatCanBeDeployed(t *testing.T) {
	var stderr strings.Builder
	cmd := exec.Command("bash", "deploy.sh", "--services")
	cmd.Stderr = &stderr
	out, err := cmd.Output()
	if err != nil {
		t.Fatalf("deploy.sh --services: %v\nstderr:\n%s", err, stderr.String())
	}
	if note := strings.TrimSpace(stderr.String()); note != "" {
		t.Errorf("deploy.sh --services warned %q. Every deployable service carries a "+
			"com.muchq.description label, so this note means either a label was dropped or the "+
			"check is counting services that were never deployable.", note)
	}

	listed := map[string]bool{}
	for i, line := range strings.Split(strings.TrimSpace(string(out)), "\n") {
		if i == 0 {
			continue // SERVICE / DESCRIPTION header
		}
		fields := strings.Fields(line)
		if len(fields) == 0 {
			continue
		}
		if len(fields) == 1 {
			t.Errorf("deploy.sh --services lists %q with no description; the label is what the "+
				"listing is for.", fields[0])
		}
		listed[fields[0]] = true
	}

	deployable := deployableServices(t)
	for service := range deployable {
		if !listed[service] {
			t.Errorf("%s has a pinned image, so `--service %s` is accepted, but it is missing "+
				"from `--services` — the listing is how you find out it can be deployed.",
				service, service)
		}
	}
	for service := range listed {
		if !deployable[service] {
			t.Errorf("`--services` offers %q, but it carries no ghcr.io/muchq image and "+
				"`--service %s` is rejected. Listing it advertises a target that does not "+
				"exist (#1225).", service, service)
		}
	}
}

// The services deploy.sh will accept for --service: the ones whose image is
// pinned to a commit, which is the same rule the script's own service_names
// applies.
func deployableServices(t *testing.T) map[string]bool {
	t.Helper()
	deployable := map[string]bool{}
	for service, lines := range composeServiceLines(t, "compose.yaml") {
		for _, line := range lines {
			if strings.HasPrefix(strings.TrimSpace(line), "image: ghcr.io/muchq/") {
				deployable[service] = true
			}
		}
	}
	return deployable
}

// The Forgejo crawler guards (#1447).
//
// Every part of the guard is one edit away from silently doing nothing, and
// Caddy accepts all of them: a matcher that ORs instead of ANDs either opens
// the site or closes it to humans, a UA pattern that stops matching restores
// the starvation, and a guard with no `respond 403` is decoration. Nothing
// else in the stack reports any of that.

// caddyMatcherBody returns the directives of a named matcher, written either
// as a one-liner (`@name header_regexp …`) or as a block (`@name { … }`).
func caddyMatcherBody(t *testing.T, site []string, matcher string) []string {
	t.Helper()
	for i, line := range site {
		if line == matcher+" {" {
			return caddyBlockAt(site, i)
		}
		if strings.HasPrefix(line, matcher+" ") {
			return []string{strings.TrimSpace(strings.TrimPrefix(line, matcher))}
		}
	}
	t.Fatalf("no %q matcher in the git.muchq.com block. Block was:\n%s",
		matcher, strings.Join(site, "\n"))
	return nil
}

// caddyMatcherRegexp compiles the pattern out of a header_regexp/path_regexp
// directive, so these tests assert on what Caddy will actually match rather
// than on the spelling of the line.
func caddyMatcherRegexp(t *testing.T, directives []string, directive string) *regexp.Regexp {
	t.Helper()
	for _, line := range directives {
		fields := strings.Fields(line)
		if len(fields) < 2 || fields[0] != directive {
			continue
		}
		pattern := fields[len(fields)-1]
		compiled, err := regexp.Compile(pattern)
		if err != nil {
			t.Fatalf("%s pattern %q does not compile: %v", directive, pattern, err)
		}
		return compiled
	}
	t.Fatalf("no %s directive among %q", directive, directives)
	return nil
}

// Pinned verbatim: a pattern that stops matching this string is the starvation
// again, and it would look like a passing suite.
const metaCrawlerUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
	"(KHTML, like Gecko) Chrome/145.0.0.0 Safari/537.36 (compatible; meta-externalagent/1.1 " +
	"(+https://developers.facebook.com/docs/sharing/webmasters/crawler))"

// caddySnippet returns the lines inside the named top-level snippet.
func caddySnippet(t *testing.T, name, snippet string) []string {
	t.Helper()
	lines := directiveLines(t, name)
	for i, line := range lines {
		if line == "("+snippet+") {" {
			return caddyBlockAt(lines, i)
		}
	}
	t.Fatalf("no (%s) snippet in %s", snippet, name)
	return nil
}

// Nothing this crawler fetches here is welcome on any host, so the guard lives
// in the snippet every site imports.
func TestEveryHostBlocksTheCrawlerThatPeggedForgejoByBothUserAgentAndSubnet(t *testing.T) {
	site := caddySnippet(t, "Caddyfile", "refuse_bots")

	ua := caddyMatcherRegexp(t, caddyMatcherBody(t, site, "@meta_agent"), "header_regexp")
	if !ua.MatchString(metaCrawlerUserAgent) {
		t.Errorf("@meta_agent (%s) does not match the User-Agent captured during the "+
			"incident:\n  %s\nThat string is the whole reason the matcher exists.",
			ua, metaCrawlerUserAgent)
	}
	if ua.MatchString("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 " +
		"(KHTML, like Gecko) Chrome/145.0.0.0 Safari/537.36") {
		t.Errorf("@meta_agent (%s) matches a plain browser User-Agent; it would 403 humans.", ua)
	}

	subnet := caddyMatcherBody(t, site, "@meta_subnet")
	joined := strings.Join(subnet, "\n")
	if !strings.Contains(joined, "remote_ip 57.141.0.0/16") {
		t.Errorf("@meta_subnet does not declare `remote_ip 57.141.0.0/16`, got %q. The crawl "+
			"came from ~70 addresses in that range, which is why no per-address rate limit "+
			"saw it.", subnet)
	}
	// The range is Meta's link unfurler's too. Refused everywhere except the
	// short-link redirects, or pasting an iili link into a Meta app shows no
	// preview; the exemption is the two paths and nothing wider.
	if !strings.Contains(joined, "not path /r/* /iili/v1/r/*") {
		t.Errorf("@meta_subnet does not exempt the short-link redirects, got %q; Meta's link "+
			"unfurler shares the range.", subnet)
	}

	// Both, deliberately. Either alone is one field away from useless: the
	// agent self-identifies today and could stop, and the subnet is Meta's
	// today and could move. They are redundant now — that is the point.
	for _, matcher := range []string{"@meta_agent", "@meta_subnet"} {
		if !forgejoMatcherIsRefused(site, matcher) {
			t.Errorf("%s is defined but never answered with `respond 403`; the matcher is "+
				"decoration. Block was:\n%s", matcher, strings.Join(site, "\n"))
		}
	}
	// One copy: a site block declaring its own @meta_ matcher would drift from the snippet.
	for _, line := range caddySiteBlock(t, "Caddyfile", "git.muchq.com") {
		if strings.HasPrefix(line, "@meta_") {
			t.Errorf("git.muchq.com declares %q; the guard lives in refuse_bots", line)
		}
	}
}

// forgejoMatcherIsRefused reports whether the block answers the matcher with a
// 403 rather than proxying it.
func forgejoMatcherIsRefused(site []string, matcher string) bool {
	for i, line := range site {
		if line != "handle "+matcher+" {" {
			continue
		}
		for _, inner := range caddyBlockAt(site, i) {
			if strings.HasPrefix(inner, "respond 403") {
				return true
			}
		}
	}
	return false
}

// The expensive-route guard has to AND its two conditions. Split into two
// matchers it becomes an OR, and an OR is wrong in both directions at once:
// every crawler loses the cheap pages it is welcome to read, and — far worse —
// every human loses /commit/ and /blame/, which is most of what the site is
// for.
func TestForgejoExpensiveRouteGuardAndsTheUserAgentWithThePath(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile", "git.muchq.com")
	body := caddyMatcherBody(t, site, "@crawler_on_expensive_route")

	directives := map[string]bool{"header_regexp": false, "path_regexp": false}
	for _, line := range body {
		if _, ok := directives[firstToken(line)]; ok {
			directives[firstToken(line)] = true
		}
	}
	for directive, found := range directives {
		if !found {
			t.Fatalf("@crawler_on_expensive_route declares no %s. Both conditions must live "+
				"in the one matcher block, where Caddy ANDs them: separated, a crawler UA "+
				"alone or a repo path alone would 403. Body was:\n%s",
				directive, strings.Join(body, "\n"))
		}
	}
}

func TestForgejoClosesTheRoutesThatCostSecondsToCrawlersOnly(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile", "git.muchq.com")
	body := caddyMatcherBody(t, site, "@crawler_on_expensive_route")
	ua := caddyMatcherRegexp(t, body, "header_regexp")
	path := caddyMatcherRegexp(t, body, "path_regexp")

	// The six that match invoke git. The ones that must not are a database read
	// or a file read, and stay open.
	for _, tc := range []struct {
		uri     string
		blocked bool
		why     string
	}{
		{"/andy/moonbase/commit/a01805e2887dc3476740d5d969ca7665e49ab9d7", true, "diffs a commit"},
		{"/andy/moonbase/compare/ad485f8584a5ed0157d50d40d039f157d86cc0a5...8d723165b58da50d72149a895f0308113ced1a0f", true, "diffs two arbitrary commits"},
		{"/andy/nbody/src/branch/main/README.md", true, "walks the tree"},
		{"/andy/smithy-cpp/blame/branch/main/BUILD.bazel", true, "blames every line"},
		{"/andy/moonbase/commits/branch/main", true, "walks the log"},
		{"/andy/moonbase/archive/main.tar.gz", true, "built per request"},
		{"/andy/moonbase/issues/1324", false, "a database read"},
		{"/andy/moonbase/raw/branch/main/README.md", false, "a file read"},
		{"/andy/moonbase", false, "the repo home page"},
		{"/explore/repos", false, "the listing the landing page serves"},
		{"/robots.txt", false, "the file that tells honest crawlers what to skip"},
		{"/assets/js/index.js", false, "static assets the browser needs"},
	} {
		if got := path.MatchString(tc.uri); got != tc.blocked {
			t.Errorf("path_regexp matched %q = %v, want %v (%s)", tc.uri, got, tc.blocked, tc.why)
		}
	}

	// The UA half stays narrow enough that a browser keeps every route.
	for _, agent := range []string{
		metaCrawlerUserAgent,
		"Mozilla/5.0 (compatible; GPTBot/1.2; +https://openai.com/gptbot)",
		"Mozilla/5.0 (compatible; ClaudeBot/1.0; +claudebot@anthropic.com)",
		"Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)",
		"Mozilla/5.0 (compatible; DotBot/1.2; +https://opensiteexplorer.org/dotbot)",
	} {
		if !ua.MatchString(agent) {
			t.Errorf("header_regexp (%s) does not match self-identified crawler %q", ua, agent)
		}
	}
	for _, agent := range []string{
		"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/145.0.0.0 Safari/537.36",
		"Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0",
		"git/2.45.2",
	} {
		if ua.MatchString(agent) {
			t.Errorf("header_regexp (%s) matches %q, which is not a crawler; it would 403 a "+
				"human or a clone.", ua, agent)
		}
	}
}

// The guards are refusals layered in front of the proxy, not a replacement for
// it: everything unmatched still has to reach Forgejo.
func TestForgejoStillProxiesEverythingTheGuardsDoNotRefuse(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile", "git.muchq.com")

	if ok, problem := catchAllIsLastHandle(site, "reverse_proxy forgejo:3000"); !ok {
		t.Fatalf("%s in the git.muchq.com block. With the guards written as handle blocks, "+
			"an unmatched request falls through to nothing and the site answers 200 with an "+
			"empty body — or, placed above a guard, the catch-all disables the 403. Block was:\n%s",
			problem, strings.Join(site, "\n"))
	}
}

// Container stdout is a disk-fill risk on a single host: docker's json-file
// driver keeps everything by default, and nothing rotates it. Every service
// caps its logs by merging the shared x-default-logging anchor (#1456). The
// healthcheck guard forbids anchors because it must read commands as text;
// this block carries no command, so the shared anchor is the point rather
// than a hole — one place to change the cap, and this test only requires
// that each service declares the key.
func TestEveryServiceCapsItsContainerLogs(t *testing.T) {
	// The per-service key without the cap is the same unbounded state
	// wearing a seatbelt: the anchor itself must bound size and count, or
	// deleting two lines from it silently reverts all 18 services at once.
	anchor := false
	for _, line := range activeLines(t, "compose.yaml") {
		if line == "x-default-logging: &default-logging" {
			anchor = true
		}
	}
	if !anchor {
		t.Fatal("compose.yaml no longer defines the x-default-logging anchor")
	}
	for _, bound := range []string{"max-size:", "max-file:"} {
		if !hasLinePrefix(activeLines(t, "compose.yaml"), bound) {
			t.Errorf("the logging anchor sets no %s — every container is back to unbounded "+
				"json-file logs, which is the disk-fill #1456 exists to close", bound)
		}
	}

	services := composeServiceLines(t, "compose.yaml")
	if len(services) < 10 {
		t.Fatalf("parsed only %d services out of compose.yaml; the parser has gone stale", len(services))
	}
	for service, lines := range services {
		found := false
		for _, line := range lines {
			if strings.HasPrefix(strings.TrimSpace(line), "logging:") {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("%s declares no logging cap; its stdout grows until the disk fills. "+
				"Add `logging: *default-logging` (#1456).", service)
		}
	}
}

// The shipper reads Caddy's rolled access logs off the same host path Caddy
// writes them to, and deletes what it has uploaded — so the mount must be the
// host bind, and must not be read-only (#1457). Profile-gated: the service
// needs S3 credentials in ~/.env, so it stays out of a default `up -d` until
// the operator opts in.
func TestLogShipperReadsTheCaddyLogMountAndIsProfileGated(t *testing.T) {
	block := serviceBlock(t, "compose.yaml", "log_shipper")

	if !strings.Contains(block, "ghcr.io/muchq/log_shipper") {
		t.Fatalf("did not find log_shipper's image in its compose block; this test is no longer "+
			"reading the service it claims to. Block was:\n%s", block)
	}
	if !strings.Contains(block, "- /var/log/caddy:/var/log/caddy") {
		t.Errorf("log_shipper does not bind-mount /var/log/caddy; it has nothing to ship. "+
			"Block was:\n%s", block)
	}
	if !strings.Contains(block, "caddy=/var/log/caddy") {
		t.Errorf("log_shipper's LOG_DIRS does not name caddy=/var/log/caddy; the access logs "+
			"stop shipping while the mount looks fine. Block was:\n%s", block)
	}
	if strings.Contains(block, "/var/log/caddy:/var/log/caddy:ro") {
		t.Errorf("log_shipper mounts the log dir read-only; it deletes rolled files after " +
			"upload, so read-only, every pass fails the delete and re-uploads the same " +
			"rolls forever, and retention silently falls back to Caddy's roll_keep.")
	}
	if !strings.Contains(block, "profiles:") {
		t.Errorf("log_shipper is not profile-gated; a default `docker compose up -d` would "+
			"start it with no S3 credentials and it would crash-loop. Block was:\n%s", block)
	}
}

// one_d4 rolls its query events into a host directory and the shipper moves
// them under their own partition (#1465): both containers bind the same
// directory, and the shipper names it with its label.
func TestOneD4QueryEventsAreRolledWhereTheShipperReads(t *testing.T) {
	oneD4 := serviceBlock(t, "compose.yaml", "one_d4")
	if !strings.Contains(oneD4, "QUERY_EVENT_LOG_DIR=/var/log/one_d4") {
		t.Errorf("one_d4 does not set QUERY_EVENT_LOG_DIR; the query events roll into the "+
			"container's scratch default and never leave it. Block was:\n%s", oneD4)
	}
	if !strings.Contains(oneD4, "- /var/log/one_d4:/var/log/one_d4") {
		t.Errorf("one_d4 does not bind-mount /var/log/one_d4; its rolls die with the container. "+
			"Block was:\n%s", oneD4)
	}

	shipper := serviceBlock(t, "compose.yaml", "log_shipper")
	if !strings.Contains(shipper, "one_d4=/var/log/one_d4") {
		t.Errorf("log_shipper's LOG_DIRS does not name one_d4=/var/log/one_d4; the rolls pile "+
			"up unshipped. Block was:\n%s", shipper)
	}
	if !strings.Contains(shipper, "- /var/log/one_d4:/var/log/one_d4") {
		t.Errorf("log_shipper does not bind-mount /var/log/one_d4. Block was:\n%s", shipper)
	}
	if strings.Contains(shipper, "/var/log/one_d4:/var/log/one_d4:ro") {
		t.Errorf("log_shipper mounts one_d4's log dir read-only; it deletes rolled files after " +
			"upload, so every pass would re-upload the same rolls forever.")
	}
}

// The stats pair is profile-gated together: the aggregator needs the same
// S3 credentials the shipper does, so a default `up -d` must start
// neither the service nor its db-init — half the pair running is a
// crash-loop or a database nothing writes to.
func TestTheStatsPairIsProfileGatedTogether(t *testing.T) {
	for _, service := range []string{"stats", "stats_db_init"} {
		block := serviceBlock(t, "compose.yaml", service)
		if !strings.Contains(block, "profiles:") {
			t.Errorf("%s is not profile-gated; a default `up -d` starts it without "+
				"S3 credentials (#1460). Block was:\n%s", service, block)
		}
	}
	if !strings.Contains(serviceBlock(t, "compose.yaml", "stats"), "postgresql://stats:") {
		t.Errorf("stats names no stats database URL; the aggregates have nowhere to land")
	}
}

// catchAllIsLastHandle reports whether the site block ends its handle
// chain with `handle { <terminal> }`. Handle blocks are mutually exclusive
// and run in written order, so a catch-all anywhere but last shadows every
// handle after it; the position is the correctness argument, not a style.
func catchAllIsLastHandle(site []string, terminal string) (found bool, problem string) {
	lastHandle, catchAll := -1, -1
	for i, line := range site {
		if line == "handle {" || strings.HasPrefix(line, "handle @") {
			lastHandle = i
		}
		if line != "handle {" {
			continue
		}
		for _, inner := range caddyBlockAt(site, i) {
			if inner == terminal && catchAll < 0 {
				catchAll = i // the first one is the one that shadows
			}
		}
	}
	switch {
	case catchAll < 0:
		return false, "no catch-all `handle { " + terminal + " }`"
	case catchAll != lastHandle:
		return false, "the catch-all is not the last handle; the handles after it are shadowed"
	}
	return true, ""
}

// The local gateway's routes, matcher to upstream, so a transposed port in
// a rewrite of the block does not ship.
var localRoutes = map[string]string{
	"@ws_thoughts":          "localhost:8080",
	"@post_golf_v2_session": "localhost:8089",
	"@ws_golf_v2":           "localhost:8089",
	"@ws_thoughts_v2":       "localhost:8089",
	"@post_portrait":        "localhost:8081",
	"@get_metrics":          "localhost:8082",
	"@post_mithril":         "localhost:8083",
	"@post_posterize_blur":  "localhost:8084",
	"@post_posterize_edges": "localhost:8084",
}

// api.muchq.com and gpt.muchq.com answer only the routes they declare; an
// unmatched path, or a HEAD on a GET-only matcher, is a 404 — not the empty
// 200 Caddy hands out when nothing handles a request (#1468). The stats
// pipeline's probe table counts sub-400 answers as "served", so on these
// hosts every scanner probe read as answered. The catch-all has to be a
// handle block, and every proxied route has to be one too: Caddy orders
// handle before reverse_proxy, so a bare `reverse_proxy @matcher` beside a
// catch-all handle is never reached.
func TestGatewayHostsAnswerUnmatchedPathsWith404(t *testing.T) {
	for _, tc := range []struct{ file, host string }{
		{"Caddyfile", "api.muchq.com"},
		{"Caddyfile", "gpt.muchq.com"},
		{"Caddyfile.local", ":2015"},
	} {
		t.Run(tc.host, func(t *testing.T) {
			site := caddySiteBlock(t, tc.file, tc.host)
			for _, line := range site {
				if strings.HasPrefix(line, "reverse_proxy @") {
					t.Errorf("bare %q would be shadowed by the catch-all handle; wrap it in "+
						"`handle @matcher { reverse_proxy upstream }`.", line)
				}
			}
			if ok, problem := catchAllIsLastHandle(site, "respond 404"); !ok {
				t.Errorf("%s in the %s block; an unmatched request is answered 200 with an "+
					"empty body, or a real route is. Block was:\n%s",
					problem, tc.host, strings.Join(site, "\n"))
			}
		})
	}
}

func TestLocalGatewayRoutesReachTheirPorts(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile.local", ":2015")
	seen := map[string]bool{}
	for i, line := range site {
		if !strings.HasPrefix(line, "handle @") {
			continue
		}
		matcher := strings.TrimSuffix(strings.TrimPrefix(line, "handle "), " {")
		upstream, routed := localRoutes[matcher]
		if !routed {
			continue // @options and anything else that is not a proxy
		}
		seen[matcher] = true
		for _, inner := range caddyBlockAt(site, i) {
			if strings.HasPrefix(inner, "reverse_proxy ") && inner != "reverse_proxy "+upstream {
				t.Errorf("%s reaches %q locally, want %s", matcher, inner, upstream)
			}
		}
	}
	for matcher := range localRoutes {
		if !seen[matcher] {
			t.Errorf("the local gateway has no handle block for %s", matcher)
		}
	}
}

// one_d4 books a query as the web app's by the Origin the browser attaches,
// and the only Origin the api.1d4.net block lets through CORS is the one it
// grants Access-Control-Allow-Origin to. The two are spelled in two files;
// the day the app moves origin, every UI query would silently read as `api`.
func TestOneD4KnowsTheUiOriginCaddyGrants(t *testing.T) {
	site := caddySiteBlock(t, "Caddyfile", "api.1d4.net")
	var granted []string
	for _, line := range site {
		if strings.HasPrefix(line, "Access-Control-Allow-Origin ") {
			granted = append(granted, strings.Trim(strings.TrimPrefix(line, "Access-Control-Allow-Origin "), `"`))
		}
	}
	if len(granted) == 0 {
		t.Fatal("api.1d4.net grants no Access-Control-Allow-Origin; the web app cannot call it")
	}
	source, err := os.ReadFile("../../domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/api/QueryEvent.java")
	if err != nil {
		t.Fatal(err)
	}
	for _, origin := range granted {
		if !strings.Contains(string(source), `"`+origin+`"`) {
			t.Errorf("Caddy grants origin %s but QueryEvent.UI_ORIGINS does not list it; its queries "+
				"would be counted as direct API calls.", origin)
		}
	}
}

// TLM-Audit-Scanner (#1458) walks every vhost for exposed credentials and
// ignores robots.txt. The refusals are a snippet each site imports first, so a
// new site block cannot forget it and no site answers a refused guest before
// the import runs; UA and address both, for the same reason the meta guard
// carries both.
func TestEverySiteRefusesTheCredentialScanner(t *testing.T) {
	lines := directiveLines(t, "Caddyfile")

	var snippet []string
	depth := 0
	var sites []string
	for i, line := range lines {
		opened := strings.Count(line, "{") - strings.Count(line, "}")
		if depth == 0 && opened == 1 {
			switch {
			case line == "(refuse_bots) {":
				snippet = caddyBlockAt(lines, i)
			case line == "{" || strings.HasPrefix(line, "("):
				// Global options and other snippets are not sites.
			default:
				sites = append(sites, line)
				// Handle blocks run in written order, so the import has to sit above every
				// handle the site declares; directives Caddy orders ahead of handle (header,
				// rewrite, redir) may precede it, and a refusal still carries the site's
				// CORS headers, which is right.
				importAt, firstHandleAt := -1, -1
				for j, inner := range caddyBlockAt(lines, i) {
					if inner == "import refuse_bots" && importAt < 0 {
						importAt = j
					}
					if strings.HasPrefix(inner, "handle") && firstHandleAt < 0 {
						firstHandleAt = j
					}
				}
				if importAt < 0 {
					t.Errorf("%s does not import refuse_bots; a refused guest is answered there.", line)
				} else if firstHandleAt >= 0 && firstHandleAt < importAt {
					t.Errorf("%s imports refuse_bots below its own handle blocks; a refused guest "+
						"matching an earlier handle is answered before it is refused.", line)
				}
			}
		}
		depth += opened
	}
	if snippet == nil {
		t.Fatal("no (refuse_bots) snippet in the Caddyfile")
	}
	if len(sites) < 5 {
		t.Fatalf("found only %d site blocks; the walk over the file is wrong", len(sites))
	}
	// The local file carries a copy of the snippet, and the local probe is only
	// evidence about production while the two are the same text.
	if local := caddySnippet(t, "Caddyfile.local", "refuse_bots"); strings.Join(local, "\n") != strings.Join(snippet, "\n") {
		t.Errorf("Caddyfile.local's refuse_bots differs from the Caddyfile's:\n%s\n-- vs --\n%s",
			strings.Join(local, "\n"), strings.Join(snippet, "\n"))
	}

	var agent *regexp.Regexp
	addresses := ""
	for _, line := range snippet {
		if strings.HasPrefix(line, "@scanner_agent header_regexp User-Agent ") {
			agent = regexp.MustCompile(strings.TrimPrefix(line, "@scanner_agent header_regexp User-Agent "))
		}
		if strings.HasPrefix(line, "@scanner_address remote_ip ") {
			addresses = strings.TrimPrefix(line, "@scanner_address remote_ip ")
		}
	}
	if agent == nil {
		t.Fatal("refuse_bots has no @scanner_agent User-Agent matcher")
	}
	for _, ua := range []string{"TLM-Audit-Scanner/1.0", "tlm-audit-scanner"} {
		if !agent.MatchString(ua) {
			t.Errorf("the User-Agent regexp (%s) does not match %q", agent, ua)
		}
	}
	for _, ua := range []string{
		"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/145.0.0.0 Safari/537.36",
		"git/2.45.2",
		"mcpserver",
		"Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)",
	} {
		if agent.MatchString(ua) {
			t.Errorf("the User-Agent regexp (%s) matches %q, which is not the scanner", agent, ua)
		}
	}
	// The two Techoff SRV addresses the report names; the agent guard is the
	// one that survives a move, so both are pinned rather than the range.
	for _, ip := range []string{"195.178.110.199", "45.148.10.67"} {
		if !strings.Contains(addresses, ip) {
			t.Errorf("refuse_bots does not refuse %s", ip)
		}
	}
	for _, matcher := range []string{"@scanner_agent", "@scanner_address"} {
		found := false
		for i, line := range snippet {
			if line == "handle "+matcher+" {" {
				for _, inner := range caddyBlockAt(snippet, i) {
					if inner == "respond 403" {
						found = true
					}
				}
			}
		}
		if !found {
			t.Errorf("refuse_bots has no `handle %s { respond 403 }`; a matcher with no handle refuses nothing", matcher)
		}
	}
}

// Caddy rolls the access log by size only, and the stats pipeline ships
// rolled files: the roll size is the pipeline's latency. Bounded so a
// "make it roll less" edit does not quietly turn the by-day stats into a
// by-fortnight report.
func TestCaddyAccessLogRollsSmallEnoughToShipDaily(t *testing.T) {
	caddyfile := readConfig(t, "Caddyfile")
	rolls := regexp.MustCompile(`(?m)^\s*roll_size\s+(\d+)\s*([kmg]i?b)\s*$`).FindAllStringSubmatch(caddyfile, -1)
	if len(rolls) == 0 {
		t.Fatal("no roll_size in the Caddyfile: Caddy's default is 100 MiB, weeks of traffic between rolls")
	}
	unit := map[string]int64{"kb": 1 << 10, "kib": 1 << 10, "mb": 1 << 20, "mib": 1 << 20, "gb": 1 << 30, "gib": 1 << 30}
	for _, roll := range rolls {
		n, _ := strconv.ParseInt(roll[1], 10, 64)
		if size := n * unit[strings.ToLower(roll[2])]; size > 16<<20 {
			t.Errorf("roll_size %s %s: over 16 MiB the access log takes days to roll at current "+
				"traffic, and nothing ships until it does", roll[1], roll[2])
		}
	}
}
