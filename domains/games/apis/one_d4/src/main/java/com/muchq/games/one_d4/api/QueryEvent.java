package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.parser.ParseException;
import com.muchq.games.chessql.parser.ParsedQuery;
import io.micronaut.core.annotation.Nullable;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.spi.LoggingEventBuilder;

/**
 * One query or aggregate request as the stats pipeline sees it (#1465). Built up by the handler
 * inside {@link QueryEvents#observe}, and written once when the handler leaves: a structured log
 * line on a logger of its own, so the shipped JSON carries the fields and a consumer selects the
 * lines by logger name; plus the bounded part of it — entry, source, outcome, cache — as a counter
 * and a duration histogram, which is where per-minute rates and latency belong (#1460).
 *
 * <p>Nothing caller-identifying and nothing caller-authored rides along: the query text, the
 * player, and comparison values stay out. {@link QueryShape} is the whole of what the query
 * contributes, and the group-by names are logged only once the compiler has accepted them.
 */
final class QueryEvent {
  static final String LOGGER = "com.muchq.games.one_d4.query_event";
  static final String MESSAGE = "query_event";

  static final String ENTRY_QUERY = "query";
  static final String ENTRY_AGGREGATE = "aggregate";

  /** Who asked: mcpserver, the 1d4.net web app, or anyone else hitting the API directly. */
  static final String SOURCE_MCP = "mcp";

  static final String SOURCE_UI = "ui";
  static final String SOURCE_API = "api";

  static final String OUTCOME_OK = "ok";
  static final String OUTCOME_INVALID = "invalid";
  static final String OUTCOME_FAILED = "failed";

  /** Served through the first-page snapshot, warm or loading; anything else ran live. */
  static final String CACHE_SNAPSHOT = "snapshot";

  static final String CACHE_LIVE = "live";

  /** The counter label for an aggregate, or a query that never reached the cache decision. */
  static final String CACHE_NONE = "none";

  /** mcpserver sends this User-Agent product token on every call (OneD4Client). */
  static final String MCPSERVER_AGENT = "mcpserver";

  /**
   * The browser attaches Origin to the web app's cross-origin calls; nothing else has these. The
   * production value must match the Access-Control-Allow-Origin Caddy grants api.1d4.net, which
   * deploy_config_test.go checks against this file.
   */
  static final Set<String> UI_ORIGINS = Set.of("https://1d4.net", "http://localhost:5173");

  private static final Logger LOG = LoggerFactory.getLogger(LOGGER);

  private final QueryEvents owner;
  private final String entry;
  private final String source;
  private final Map<String, Object> fields = new LinkedHashMap<>();
  private final long startNanos = System.nanoTime();
  private String cache = CACHE_NONE;

  QueryEvent(QueryEvents owner, String entry, @Nullable String userAgent, @Nullable String origin) {
    this.owner = owner;
    this.entry = entry;
    this.source = sourceOf(userAgent, origin);
    fields.put("entry", entry);
    fields.put("source", source);
  }

  /**
   * mcpserver identifies itself by User-Agent and wins over Origin, which it never sends; the web
   * app is known by the Origin the browser attaches; everything else is a direct API caller.
   */
  static String sourceOf(@Nullable String userAgent, @Nullable String origin) {
    if (userAgent != null && userAgent.startsWith(MCPSERVER_AGENT)) {
      return SOURCE_MCP;
    }
    if (origin != null && UI_ORIGINS.contains(origin)) {
      return SOURCE_UI;
    }
    return SOURCE_API;
  }

  /** A blank player is no player, the way the cache, the compiler, and the spec all read it. */
  static boolean hasPlayer(@Nullable String player) {
    return player != null && !player.isBlank();
  }

  QueryEvent shape(ParsedQuery parsed) {
    QueryShape shape = QueryShape.of(parsed);
    fields.put("fields", String.join(",", shape.fields()));
    fields.put("motifs", String.join(",", shape.motifs()));
    fields.put("order_by", shape.orderBy());
    return this;
  }

  QueryEvent cache(String cache) {
    this.cache = cache;
    fields.put("cache", cache);
    return this;
  }

  QueryEvent put(String key, Object value) {
    fields.put(key, value);
    return this;
  }

  QueryEvent put(String key, List<String> values) {
    fields.put(key, String.join(",", values));
    return this;
  }

  /** Writes the line and records the metrics. {@link QueryEvents#observe} calls it exactly once. */
  void finish(String outcome) {
    long durationUs = (System.nanoTime() - startNanos) / 1_000;
    fields.put("outcome", outcome);
    fields.put("duration_us", durationUs);
    LoggingEventBuilder builder = LOG.atInfo();
    for (Map.Entry<String, Object> field : fields.entrySet()) {
      builder = builder.addKeyValue(field.getKey(), field.getValue());
    }
    builder.log(MESSAGE);
    owner.record(entry, source, outcome, cache, durationUs);
  }

  /**
   * A request the caller got wrong (validation, a query that does not parse) versus one the service
   * could not answer — a store failure, a 404-class lookup, or an Error unwinding the handler. The
   * error handler maps the same two classes to 400.
   */
  static String outcomeOf(Throwable e) {
    if (e instanceof IllegalArgumentException || e instanceof ParseException) {
      return OUTCOME_INVALID;
    }
    return OUTCOME_FAILED;
  }
}
