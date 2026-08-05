package com.muchq.games.mcpserver.api;

import io.micronaut.http.MediaType;
import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Get;
import io.micronaut.http.annotation.Produces;
import java.util.Map;

/**
 * Static liveness probe for the compose healthcheck (#1307). mcpserver had no health endpoint at
 * all, so its container could never be probed and its metric series died with every deploy until
 * the first real MCP request arrived.
 *
 * <p>Deliberately static — no database check. A store-gated probe would mark this container
 * unhealthy whenever the shared Postgres blips, and unhealthy is what health-conditioned tooling
 * acts on (compose's own service_healthy gating today; anything orchestration-shaped later, where
 * unhealthy does mean restart) — while mcpserver itself stays up serving errors, which no restart
 * improves. The one_d4 pattern is the same: /health answers 200 and puts any dependency state in
 * the body.
 *
 * <p>The path must stay exactly "/health": prom_proxy subtracts {@code route="/health"} from every
 * Serving number and selects it on the Probes tile, and the compose healthcheck probes the same
 * literal — deploy/consolidated's config test pins that side.
 */
@Controller("/health")
public class HealthController {
  @Get
  @Produces(MediaType.APPLICATION_JSON)
  public Map<String, String> health() {
    return Map.of("status", "UP");
  }
}
