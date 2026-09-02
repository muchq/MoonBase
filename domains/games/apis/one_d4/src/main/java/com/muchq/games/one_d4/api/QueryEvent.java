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
 * One structured log line per query or aggregate request, for the stats pipeline (#1465): where the
 * request came from, the shape of the query, how it was served, and how it went. Emitted as
 * key-value pairs on a logger of its own, so the shipped JSON carries them as fields and a consumer
 * can select the lines by logger name without pattern-matching messages.
 *
 * <p>Nothing caller-identifying and nothing caller-authored rides along: the query text, the
 * player, and comparison values stay out. {@link QueryShape} is the whole of what the query
 * contributes.
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

  static final String CACHE_SNAPSHOT = "snapshot";
  static final String CACHE_LIVE = "live";

  /** mcpserver sends this User-Agent product token on every call (OneD4Client). */
  static final String MCPSERVER_AGENT = "mcpserver";

  /** The browser sends Origin on the web app's cross-origin calls; nothing else has these. */
  static final Set<String> UI_ORIGINS = Set.of("https://1d4.net", "http://localhost:5173");

  private static final Logger LOG = LoggerFactory.getLogger(LOGGER);

  private final Map<String, Object> fields = new LinkedHashMap<>();
  private final long startNanos = System.nanoTime();

  private QueryEvent(String entry, String source) {
    fields.put("entry", entry);
    fields.put("source", source);
  }

  static QueryEvent start(String entry, @Nullable String userAgent, @Nullable String origin) {
    return new QueryEvent(entry, sourceOf(userAgent, origin));
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

  QueryEvent shape(ParsedQuery parsed) {
    QueryShape shape = QueryShape.of(parsed);
    fields.put("fields", String.join(",", shape.fields()));
    fields.put("motifs", String.join(",", shape.motifs()));
    fields.put("order_by", shape.orderBy());
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

  /** Writes the line. Call exactly once, on every path out of the handler. */
  void finish(String outcome) {
    fields.put("outcome", outcome);
    fields.put("duration_us", (System.nanoTime() - startNanos) / 1_000);
    LoggingEventBuilder builder = LOG.atInfo();
    for (Map.Entry<String, Object> field : fields.entrySet()) {
      builder = builder.addKeyValue(field.getKey(), field.getValue());
    }
    builder.log(MESSAGE);
  }

  /**
   * A request the caller got wrong (validation, a query that does not parse) versus one the service
   * could not answer. The error handler maps the same two classes to 400.
   */
  static String outcomeOf(RuntimeException e) {
    if (e instanceof IllegalArgumentException || e instanceof ParseException) {
      return OUTCOME_INVALID;
    }
    return OUTCOME_FAILED;
  }
}
