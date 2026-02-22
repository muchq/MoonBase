package com.muchq.games.one_d4.api;

import jakarta.inject.Singleton;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import java.sql.Connection;
import java.util.Map;
import javax.sql.DataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Path("/health")
public class HealthController {
  private static final Logger LOG = LoggerFactory.getLogger(HealthController.class);

  private final DataSource dataSource;

  public HealthController(DataSource dataSource) {
    this.dataSource = dataSource;
  }

  @GET
  @Produces(MediaType.APPLICATION_JSON)
  public Map<String, Object> health() {
    boolean dbUp = checkDatabase();
    String status = dbUp ? "UP" : "DOWN";
    return Map.of("status", status, "checks", Map.of("database", dbUp ? "UP" : "DOWN"));
  }

  private boolean checkDatabase() {
    try (Connection conn = dataSource.getConnection()) {
      return conn.isValid(2);
    } catch (Exception e) {
      LOG.warn("Health check: database unreachable", e);
      return false;
    }
  }
}
