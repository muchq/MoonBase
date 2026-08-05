package com.muchq.games.one_d4.api;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;

/** A clock tests can advance, e.g. past FirstPageCache's freshness window. */
final class MutableClock extends Clock {
  private Instant now;

  MutableClock(Instant start) {
    this.now = start;
  }

  void advance(Duration d) {
    now = now.plus(d);
  }

  @Override
  public Instant instant() {
    return now;
  }

  @Override
  public ZoneId getZone() {
    return ZoneOffset.UTC;
  }

  @Override
  public Clock withZone(ZoneId zone) {
    return this;
  }
}
