package com.muchq.games.one_d4.service;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class IndexRequestServiceTest {

  private InMemoryRequestStore requestStore;
  private RecordingQueue queue;
  private List<IndexMessage> inlineProcessed;
  private IndexRequestService service;

  @BeforeEach
  public void setUp() {
    requestStore = new InMemoryRequestStore();
    queue = new RecordingQueue();
    inlineProcessed = new ArrayList<>();
    service =
        new IndexRequestService(
            requestStore,
            queue,
            message -> {
              inlineProcessed.add(message);
              requestStore.updateStatus(message.requestId(), "COMPLETED", null, 42);
            });
  }

  private static IndexRequestService.Submission submission(String player, String platform) {
    return new IndexRequestService.Submission(player, platform, "2024-01", "2024-03", false, false);
  }

  @Test
  public void submit_createsEnqueuesAndReturnsPending() {
    IndexResponse response = service.submit(submission("hikaru", "CHESS_COM"));

    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(response.player()).isEqualTo("hikaru");
    assertThat(queue.enqueued).hasSize(1);
    assertThat(inlineProcessed).isEmpty();
  }

  @Test
  public void submit_normalizesPlayerToLowercaseForDedupeAndCacheKeys() {
    IndexResponse first = service.submit(submission("Hikaru", "CHESS_COM"));
    IndexResponse second = service.submit(submission("HIKARU", "chess.com"));

    assertThat(first.player()).isEqualTo("hikaru");
    // Same lowercased identity → deduped onto the existing PENDING request
    assertThat(second.id()).isEqualTo(first.id());
    assertThat(queue.enqueued).hasSize(1);
  }

  @Test
  public void submit_acceptsChessComPlatformSpelling() {
    IndexResponse response = service.submit(submission("hikaru", "chess.com"));
    assertThat(response.platform()).isEqualTo("CHESS_COM");
    assertThat(queue.enqueued.get(0).platform()).isEqualTo("CHESS_COM");
  }

  @Test
  public void submit_skipCacheBypassesDedupeAndPropagates() {
    service.submit(submission("hikaru", "CHESS_COM"));

    IndexResponse forced =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-03", false, true));

    assertThat(queue.enqueued).hasSize(2);
    assertThat(queue.enqueued.get(1).skipCache()).isTrue();
    assertThat(forced.status()).isEqualTo("PENDING");
  }

  @Test
  public void submitHybrid_singleMonthRunsInlineAndReturnsFinalStatus() {
    IndexResponse response =
        service.submitHybrid(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    assertThat(inlineProcessed).hasSize(1);
    assertThat(queue.enqueued).isEmpty();
    assertThat(response.status()).isEqualTo("COMPLETED");
    assertThat(response.gamesIndexed()).isEqualTo(42);
  }

  @Test
  public void submitHybrid_multiMonthEnqueues() {
    IndexResponse response = service.submitHybrid(submission("hikaru", "CHESS_COM"));

    assertThat(inlineProcessed).isEmpty();
    assertThat(queue.enqueued).hasSize(1);
    assertThat(response.status()).isEqualTo("PENDING");
  }

  @Test
  public void submit_acceptsFullTwelveMonthRange() {
    IndexResponse response =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-12", false, false));

    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(queue.enqueued).hasSize(1);
  }

  @Test
  public void submitHybrid_dedupeHitReturnsExistingWithoutRunningInline() {
    IndexResponse first =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    IndexResponse second =
        service.submitHybrid(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    assertThat(second.id()).isEqualTo(first.id());
    assertThat(inlineProcessed).isEmpty();
    assertThat(queue.enqueued).hasSize(1); // only the original submit's message
  }

  @Test
  public void submitHybrid_inlineFailurePropagatesFailedStatusAndError() {
    IndexRequestService failing =
        new IndexRequestService(
            requestStore,
            queue,
            message ->
                requestStore.updateStatus(message.requestId(), "FAILED", "chess.com 429", 0));

    IndexResponse response =
        failing.submitHybrid(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    assertThat(response.status()).isEqualTo("FAILED");
    assertThat(response.errorMessage()).isEqualTo("chess.com 429");
  }

  @Test
  public void submitHybrid_inlineProcessorThrowingPropagatesAndLeavesRowPending() {
    // Pins current behavior: an exception escaping the inline processor reaches the caller and the
    // row stays PENDING (dedupe will keep matching it — tracked as a known gap in the service).
    IndexRequestService throwing =
        new IndexRequestService(
            requestStore,
            queue,
            message -> {
              throw new RuntimeException("boom");
            });

    assertThatThrownBy(
            () ->
                throwing.submitHybrid(
                    new IndexRequestService.Submission(
                        "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false)))
        .isInstanceOf(RuntimeException.class)
        .hasMessage("boom");

    Optional<IndexingRequestStore.IndexingRequest> row =
        requestStore.findExistingRequest("hikaru", "CHESS_COM", "2024-01", "2024-01", false);
    assertThat(row).isPresent();
    assertThat(row.get().status()).isEqualTo("PENDING");
  }

  @Test
  public void validationRejectsNullFields() {
    assertThatThrownBy(() -> service.submit(submission(null, "CHESS_COM")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("player is required");
    assertThatThrownBy(() -> service.submit(submission("x", null)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("platform is required");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", null, "2024-01", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("startMonth is required");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "2024-01", null, false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("endMonth is required");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "2024-01", "June", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("endMonth must be in YYYY-MM format");
  }

  @Test
  public void validationRejectsBadInput() {
    assertThatThrownBy(() -> service.submit(submission(" ", "CHESS_COM")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("player is required");
    assertThatThrownBy(() -> service.submit(submission("x", "lichess")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unsupported platform");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "January", "2024-01", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("YYYY-MM");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "2024-05", "2024-01", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("must not be after");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "2023-01", "2024-06", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Maximum range is 12 months");
  }

  @Test
  public void status_mapsStoredRequest() {
    IndexResponse created = service.submit(submission("hikaru", "CHESS_COM"));
    requestStore.updateStatus(created.id(), "PROCESSING", null, 7);

    Optional<IndexResponse> status = service.status(created.id());

    assertThat(status).isPresent();
    assertThat(status.get().status()).isEqualTo("PROCESSING");
    assertThat(status.get().gamesIndexed()).isEqualTo(7);
    assertThat(service.status(UUID.randomUUID())).isEmpty();
  }

  private static final class InMemoryRequestStore implements IndexingRequestStore {
    private final Map<UUID, IndexingRequest> rows = new HashMap<>();

    @Override
    public UUID create(
        String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
      UUID id = UUID.randomUUID();
      rows.put(
          id,
          new IndexingRequest(
              id,
              player,
              platform,
              startMonth,
              endMonth,
              "PENDING",
              Instant.now(),
              Instant.now(),
              null,
              0,
              excludeBullet));
      return id;
    }

    @Override
    public Optional<IndexingRequest> findById(UUID id) {
      return Optional.ofNullable(rows.get(id));
    }

    @Override
    public Optional<IndexingRequest> findExistingRequest(
        String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
      return rows.values().stream()
          .filter(
              r ->
                  r.player().equals(player)
                      && r.platform().equals(platform)
                      && r.startMonth().equals(startMonth)
                      && r.endMonth().equals(endMonth)
                      && r.excludeBullet() == excludeBullet
                      && (r.status().equals("PENDING") || r.status().equals("PROCESSING")))
          .findFirst();
    }

    @Override
    public List<IndexingRequest> listRecent(int limit) {
      return List.copyOf(rows.values());
    }

    @Override
    public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
      IndexingRequest row = rows.get(id);
      rows.put(
          id,
          new IndexingRequest(
              row.id(),
              row.player(),
              row.platform(),
              row.startMonth(),
              row.endMonth(),
              status,
              row.createdAt(),
              Instant.now(),
              errorMessage,
              gamesIndexed,
              row.excludeBullet()));
    }
  }

  private static final class RecordingQueue implements IndexQueue {
    private final List<IndexMessage> enqueued = new ArrayList<>();

    @Override
    public void enqueue(IndexMessage message) {
      enqueued.add(message);
    }

    @Override
    public Optional<IndexMessage> poll(Duration timeout) {
      return Optional.empty();
    }

    @Override
    public int size() {
      return enqueued.size();
    }
  }
}
