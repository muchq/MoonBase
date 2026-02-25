package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.api.dto.ServerInfo;
import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;
import org.junit.Test;

public class ServerInfoControllerTest {

  /** Returns a fixed clock set to the given UTC time string. */
  private static Clock clockAt(String instantStr) {
    return Clock.fixed(Instant.parse(instantStr), ZoneOffset.UTC);
  }

  @Test
  public void getServerInfo_returnsEmptyWhenMoreThanOneHourBeforeMidnight() {
    // 22:59:59 UTC – just over 1 hour before midnight, outside the window
    Clock clock = clockAt("2024-06-15T22:59:59Z");
    ServerInfoController controller = new ServerInfoController(clock);

    ServerInfo info = controller.getServerInfo();

    assertThat(info.maintenanceWindows()).isEmpty();
  }

  @Test
  public void getServerInfo_returnsWindowWhenWithinOneHourOfMidnight() {
    // 23:30:00 UTC – 30 minutes before midnight
    Clock clock = clockAt("2024-06-15T23:30:00Z");
    ServerInfoController controller = new ServerInfoController(clock);

    ServerInfo info = controller.getServerInfo();

    assertThat(info.maintenanceWindows()).hasSize(1);
    assertThat(info.maintenanceWindows().get(0).message())
        .contains("midnight UTC");
    assertThat(info.maintenanceWindows().get(0).scheduledAt())
        .isEqualTo("2024-06-16T00:00:00Z");
  }

  @Test
  public void getServerInfo_returnsWindowAtExactlyOneHourBeforeMidnight() {
    // 23:00:00 UTC – exactly 1 hour before midnight (inclusive boundary)
    Clock clock = clockAt("2024-06-15T23:00:00Z");
    ServerInfoController controller = new ServerInfoController(clock);

    ServerInfo info = controller.getServerInfo();

    assertThat(info.maintenanceWindows()).hasSize(1);
  }

  @Test
  public void getServerInfo_returnsWindowOneMinuteBeforeMidnight() {
    // 23:59:00 UTC – 1 minute before midnight
    Clock clock = clockAt("2024-06-15T23:59:00Z");
    ServerInfoController controller = new ServerInfoController(clock);

    ServerInfo info = controller.getServerInfo();

    assertThat(info.maintenanceWindows()).hasSize(1);
    assertThat(info.maintenanceWindows().get(0).scheduledAt())
        .isEqualTo("2024-06-16T00:00:00Z");
  }

  @Test
  public void getServerInfo_returnsEmptyAtNoon() {
    // 12:00:00 UTC – midday, far from midnight
    Clock clock = clockAt("2024-06-15T12:00:00Z");
    ServerInfoController controller = new ServerInfoController(clock);

    ServerInfo info = controller.getServerInfo();

    assertThat(info.maintenanceWindows()).isEmpty();
  }

  @Test
  public void getServerInfo_scheduledAtPointsToNextMidnight() {
    Clock clock = clockAt("2024-06-15T23:45:00Z");
    ServerInfoController controller = new ServerInfoController(clock);

    ServerInfo info = controller.getServerInfo();

    assertThat(info.maintenanceWindows()).hasSize(1);
    // Must point to next day's midnight
    assertThat(info.maintenanceWindows().get(0).scheduledAt())
        .startsWith("2024-06-16");
  }

  @Test
  public void getServerInfo_returnsEmptyJustAfterMidnight() {
    // 00:01:00 UTC – just past midnight, over 23 hours until next midnight
    Clock clock = clockAt("2024-06-15T00:01:00Z");
    ServerInfoController controller = new ServerInfoController(clock);

    ServerInfo info = controller.getServerInfo();

    assertThat(info.maintenanceWindows()).isEmpty();
  }
}
