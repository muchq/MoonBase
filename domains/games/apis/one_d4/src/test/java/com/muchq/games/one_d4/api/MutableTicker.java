package com.muchq.games.one_d4.api;

import com.github.benmanes.caffeine.cache.Ticker;
import java.time.Duration;

/** A Caffeine ticker tests can advance, e.g. past FirstPageCache's freshness window. */
final class MutableTicker implements Ticker {
  private long nanos;

  void advance(Duration d) {
    nanos += d.toNanos();
  }

  @Override
  public long read() {
    return nanos;
  }
}
