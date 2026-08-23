package com.muchq.games.one_d4.service;

import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import java.time.Clock;
import java.time.YearMonth;
import java.time.format.DateTimeParseException;
import java.time.temporal.ChronoUnit;
import java.util.Locale;
import java.util.Optional;
import java.util.UUID;

/**
 * The single owner of the index-request lifecycle: validate → normalize → dedupe → create →
 * dispatch. Both entry points — the one_d4 REST API (IndexController) and mcpserver's in-process
 * MCP tools (IndexerFacade) — go through this service, so validation, player normalization, and
 * skip-cache semantics cannot drift between them.
 */
public class IndexRequestService {

  public static final int MAX_MONTH_SPAN = 12;

  private final IndexingRequestStore requestStore;
  private final DataAvailabilityResolver dataAvailability;
  private final Clock clock;

  public IndexRequestService(
      IndexingRequestStore requestStore, DataAvailabilityResolver dataAvailability) {
    this(requestStore, dataAvailability, Clock.systemUTC());
  }

  /**
   * @param dataAvailability required, not optional, so neither entry point can report a COMPLETED
   *     request without saying whether its games still exist. When only the REST controller
   *     enriched the response, the MCP {@code index_status} tool told agents "COMPLETED, 325 games"
   *     about a corpus retention had already deleted.
   * @param clock supplies the "now" that dedupe measures request staleness against, so a test can
   *     step over the cutoff instead of sleeping through it
   */
  public IndexRequestService(
      IndexingRequestStore requestStore, DataAvailabilityResolver dataAvailability, Clock clock) {
    this.requestStore = requestStore;
    this.dataAvailability = dataAvailability;
    this.clock = clock;
  }

  /**
   * @param skipCache refetch every month in the range instead of serving any of them from the
   *     indexed-period cache — the backfill path for rows indexed before newer columns existed.
   *     <p>It does not permit a second concurrent run of the same range. If a live request already
   *     holds that (player, platform, month range, excludeBullet), this returns that request rather
   *     than starting a rival one: two indexers over the same games interleave {@code
   *     motif_occurrences} deletes and inserts and leave every motif for those games counted twice.
   *     The caller can see it was coalesced — the response carries the in-flight request's id and
   *     its PENDING/PROCESSING status — and re-issuing once that finishes does force the refetch.
   */
  public record Submission(
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      boolean skipCache) {}

  /**
   * Starts (or reuses) an indexing request. Creating the row is the whole dispatch: the table is
   * the queue (#1279), and the C++ one_d4_worker's poller claims it — no work runs in this JVM
   * (#1389). Callers poll {@link #status}.
   */
  public IndexResponse submit(Submission submission) {
    return start(submission);
  }

  public Optional<IndexResponse> status(UUID requestId) {
    return requestStore.findById(requestId).map(this::toEnrichedResponse);
  }

  /** The request's stored fields plus whether the data it produced is still on disk. */
  public IndexResponse toEnrichedResponse(IndexingRequestStore.IndexingRequest row) {
    return toResponse(row).withData(dataAvailability.resolve(row).orElse(null));
  }

  private IndexResponse start(Submission submission) {
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

    // One question, one call. createOrAdopt settles the key first and then either adopts the row
    // that holds it or inserts, so it answers a duplicate submit as well as a fresh one, and only
    // a caller that actually created the row is allowed to dispatch work for it.
    //
    // A findExistingRequest read used to short-circuit ahead of this as an optimization, skipped
    // on skipCache. Both paths converged here anyway, so it saved a settle on duplicate submits
    // and nothing else — and once #1279 took the clock out of that read, what it saved was the
    // settle. A row nobody will ever run still reads as live, so the short-circuit answered every
    // resubmit with it and never reached the reclaim that would have retired it and freed the
    // range. The submit path is the one prompt reclaimer there is; the worker's sweep is hourly.
    IndexingRequestStore.Claim claim =
        requestStore.createOrAdopt(
            player,
            platform,
            submission.startMonth(),
            submission.endMonth(),
            submission.excludeBullet(),
            submission.skipCache(),
            RetentionPolicy.STALE_REQUEST,
            clock.instant());
    if (!claim.created()) {
      return toResponse(claim.request());
    }
    return new IndexResponse(
        claim.request().id(),
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
