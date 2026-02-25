package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.MaintenanceWindow;
import com.muchq.games.one_d4.api.dto.ServerInfo;
import jakarta.inject.Singleton;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import java.time.Clock;
import java.time.Duration;
import java.time.LocalDate;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.util.List;

@Singleton
@Path("/v1/server-info")
public class ServerInfoController {
  static final Duration MAINTENANCE_WINDOW_THRESHOLD = Duration.ofHours(1);
  static final String MAINTENANCE_MESSAGE =
      "Scheduled server restart tonight at midnight UTC. Expect brief downtime.";

  private final Clock clock;

  public ServerInfoController() {
    this(Clock.systemUTC());
  }

  ServerInfoController(Clock clock) {
    this.clock = clock;
  }

  @GET
  @Produces(MediaType.APPLICATION_JSON)
  public ServerInfo getServerInfo() {
    ZonedDateTime now = ZonedDateTime.now(clock);
    ZonedDateTime midnight =
        LocalDate.now(clock).plusDays(1).atStartOfDay(ZoneOffset.UTC);
    Duration timeUntilMidnight = Duration.between(now.toInstant(), midnight.toInstant());

    if (timeUntilMidnight.compareTo(MAINTENANCE_WINDOW_THRESHOLD) <= 0) {
      MaintenanceWindow window =
          new MaintenanceWindow(MAINTENANCE_MESSAGE, midnight.toInstant().toString());
      return new ServerInfo(List.of(window));
    }

    return new ServerInfo(List.of());
  }
}
