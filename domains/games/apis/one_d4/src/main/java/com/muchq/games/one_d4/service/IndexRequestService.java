package com.muchq.games.one_d4.service;

import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import java.time.YearMonth;
import java.time.format.DateTimeParseException;
import java.time.temporal.ChronoUnit;
import java.util.Locale;
import java.util.Optional;
import java.util.UUID;
import java.util.function.Consumer;

/**
 * The single owner of the index-request lifecycle: validate → normalize → dedupe → create →
 * dispatch. Both entry points — the one_d4 REST API (IndexController) and mcpserver's in-process
 * MCP tools (IndexerFacade) — go through this service, so validation, player normalization, and
 * skip-cache semantics cannot drift between them.
 */
public class IndexRequestService {

  public static final int MAX_MONTH_SPAN = 12;

  private final IndexingRequestStore requestStore;
  private final IndexQueue queue;
  private final Consumer<IndexMessage> inlineProcessor;
  private final DataAvailabilityResolver dataAvailability;

  /**
   * @param inlineProcessor runs a message to completion on the calling thread; used by {@link
   *     #submitHybrid} for single-month ranges (bind IndexWorker::process)
   * @param dataAvailability required, not optional, so neither entry point can report a COMPLETED
   *     request without saying whether its games still exist. When only the REST controller
   *     enriched the response, the MCP {@code index_status} tool told agents "COMPLETED, 325 games"
   *     about a corpus retention had already deleted.
   */
  public IndexRequestService(
      IndexingRequestStore requestStore,
      IndexQueue queue,
      Consumer<IndexMessage> inlineProcessor,
      DataAvailabilityResolver dataAvailability) {
    this.requestStore = requestStore;
    this.queue = queue;
    this.inlineProcessor = inlineProcessor;
    this.dataAvailability = dataAvailability;
  }

  /**
   * @param skipCache bypass the indexed-period cache and request dedupe, refetching every month in
   *     the range — the backfill path for rows indexed before newer columns existed
   */
  public record Submission(
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      boolean skipCache) {}

  /** Starts (or reuses) an indexing request; work always runs in the background. */
  public IndexResponse submit(Submission submission) {
    return start(submission, false);
  }

  /**
   * Like {@link #submit}, but single-month ranges run inline so the caller gets a final status in
   * one round trip (typically well under a minute). Longer ranges are enqueued and can be polled
   * via {@link #status}.
   */
  public IndexResponse submitHybrid(Submission submission) {
    return start(submission, true);
  }

  public Optional<IndexResponse> status(UUID requestId) {
    return requestStore.findById(requestId).map(this::toEnrichedResponse);
  }

  /** The request's stored fields plus whether the data it produced is still on disk. */
  public IndexResponse toEnrichedResponse(IndexingRequestStore.IndexingRequest row) {
    return toResponse(row).withData(dataAvailability.resolve(row).orElse(null));
  }

  private IndexResponse start(Submission submission, boolean inlineSingleMonth) {
    if (submission.player() == null || submission.player().isBlank()) {
      throw new IllegalArgumentException("player is required");
    }
    // Request dedupe and the indexed-period cache are keyed by the player string as given, so
    // normalize case here — "Hikaru" and "hikaru" must not index twice.
    String player = submission.player().strip().toLowerCase(Locale.ROOT);
    String platform = canonicalPlatform(submission.platform());
    YearMonth start = parseMonth(submission.startMonth(), "startMonth");
    YearMonth end = parseMonth(submission.endMonth(), "endMonth");
    if (start.isAfter(end)) {
      throw new IllegalArgumentException("startMonth must not be after endMonth");
    }
    long monthSpan = start.until(end, ChronoUnit.MONTHS) + 1;
    if (monthSpan > MAX_MONTH_SPAN) {
      throw new IllegalArgumentException(
          "Maximum range is " + MAX_MONTH_SPAN + " months, got " + monthSpan);
    }

    if (!submission.skipCache()) {
      Optional<IndexingRequestStore.IndexingRequest> existing =
          requestStore.findExistingRequest(
              player,
              platform,
              submission.startMonth(),
              submission.endMonth(),
              submission.excludeBullet());
      if (existing.isPresent()) {
        return toResponse(existing.get());
      }
    }

    UUID id =
        requestStore.create(
            player,
            platform,
            submission.startMonth(),
            submission.endMonth(),
            submission.excludeBullet());
    IndexMessage message =
        new IndexMessage(
            id,
            player,
            platform,
            submission.startMonth(),
            submission.endMonth(),
            submission.excludeBullet(),
            submission.skipCache());

    if (inlineSingleMonth && monthSpan <= 1) {
      inlineProcessor.accept(message);
      return status(id)
          .orElse(
              new IndexResponse(
                  id,
                  player,
                  platform,
                  submission.startMonth(),
                  submission.endMonth(),
                  "UNKNOWN",
                  0,
                  null,
                  submission.excludeBullet()));
    }

    queue.enqueue(message);
    return new IndexResponse(
        id,
        player,
        platform,
        submission.startMonth(),
        submission.endMonth(),
        "PENDING",
        0,
        null,
        submission.excludeBullet());
  }

  public static IndexResponse toResponse(IndexingRequestStore.IndexingRequest row) {
    return new IndexResponse(
        row.id(),
        row.player(),
        row.platform(),
        row.startMonth(),
        row.endMonth(),
        row.status(),
        row.gamesIndexed(),
        row.errorMessage(),
        row.excludeBullet());
  }

  private static String canonicalPlatform(String platform) {
    if (platform == null || platform.isBlank()) {
      throw new IllegalArgumentException("platform is required");
    }
    String normalized = platform.strip().toUpperCase(Locale.ROOT).replace('.', '_');
    if (!"CHESS_COM".equals(normalized)) {
      throw new IllegalArgumentException(
          "Unsupported platform: " + platform + ". Supported: chess.com (CHESS_COM)");
    }
    return "CHESS_COM";
  }

  private static YearMonth parseMonth(String value, String fieldName) {
    if (value == null || value.isBlank()) {
      throw new IllegalArgumentException(fieldName + " is required");
    }
    try {
      return YearMonth.parse(value);
    } catch (DateTimeParseException e) {
      throw new IllegalArgumentException(fieldName + " must be in YYYY-MM format, got: " + value);
    }
  }
}
