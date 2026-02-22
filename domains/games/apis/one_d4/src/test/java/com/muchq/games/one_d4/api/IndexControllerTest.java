package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.api.dto.IndexRequest;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class IndexControllerTest {

  private IndexController controller;
  private FakeIndexingRequestStore requestStore;
  private FakeIndexQueue queue;

  @Before
  public void setUp() {
    requestStore = new FakeIndexingRequestStore();
    queue = new FakeIndexQueue();
    controller = new IndexController(requestStore, queue, new IndexRequestValidator());
  }

  @Test
  public void createIndex_createsAndEnqueuesWhenNoExistingRequest() {
    IndexRequest request = new IndexRequest("hikaru", "CHESS_COM", "2024-01", "2024-03");
    IndexResponse response = controller.createIndex(request);

    assertThat(response.id()).isNotNull();
    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(response.gamesIndexed()).isEqualTo(0);
    assertThat(requestStore.createCallCount()).isEqualTo(1);
    assertThat(queue.enqueued()).hasSize(1);
    assertThat(queue.enqueued().get(0).player()).isEqualTo("hikaru");
    assertThat(queue.enqueued().get(0).platform()).isEqualTo("CHESS_COM");
    assertThat(queue.enqueued().get(0).startMonth()).isEqualTo("2024-01");
    assertThat(queue.enqueued().get(0).endMonth()).isEqualTo("2024-03");
  }

  @Test
  public void createIndex_returnsExistingRequestWhenDuplicateParams() {
    UUID existingId = UUID.randomUUID();
    requestStore.setExistingRequest(
        new IndexingRequestStore.IndexingRequest(
            existingId,
            "hikaru",
            "CHESS_COM",
            "2024-01",
            "2024-03",
            "PROCESSING",
            Instant.now(),
            Instant.now(),
            null,
            50));

    IndexRequest request = new IndexRequest("hikaru", "CHESS_COM", "2024-01", "2024-03");
    IndexResponse response = controller.createIndex(request);

    assertThat(response.id()).isEqualTo(existingId);
    assertThat(response.status()).isEqualTo("PROCESSING");
    assertThat(response.gamesIndexed()).isEqualTo(50);
    assertThat(requestStore.createCallCount()).isEqualTo(0);
    assertThat(queue.enqueued()).isEmpty();
  }

  @Test
  public void createIndex_returnsExistingPendingRequest() {
    UUID existingId = UUID.randomUUID();
    requestStore.setExistingRequest(
        new IndexingRequestStore.IndexingRequest(
            existingId,
            "player",
            "CHESS_COM",
            "2024-06",
            "2024-06",
            "PENDING",
            Instant.now(),
            Instant.now(),
            null,
            0));

    IndexResponse response =
        controller.createIndex(new IndexRequest("player", "CHESS_COM", "2024-06", "2024-06"));

    assertThat(response.id()).isEqualTo(existingId);
    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(response.gamesIndexed()).isEqualTo(0);
    assertThat(requestStore.createCallCount()).isEqualTo(0);
  }

  private static final class FakeIndexingRequestStore implements IndexingRequestStore {
    private Optional<IndexingRequestStore.IndexingRequest> existingRequest = Optional.empty();
    private int createCallCount = 0;

    void setExistingRequest(IndexingRequestStore.IndexingRequest request) {
      this.existingRequest = Optional.of(request);
    }

    int createCallCount() {
      return createCallCount;
    }

    @Override
    public UUID create(String player, String platform, String startMonth, String endMonth) {
      createCallCount++;
      return UUID.randomUUID();
    }

    @Override
    public Optional<IndexingRequestStore.IndexingRequest> findById(UUID id) {
      return Optional.empty();
    }

    @Override
    public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {}

    @Override
    public Optional<IndexingRequestStore.IndexingRequest> findExistingRequest(
        String player, String platform, String startMonth, String endMonth) {
      return existingRequest.filter(
          r ->
              r.player().equals(player)
                  && r.platform().equals(platform)
                  && r.startMonth().equals(startMonth)
                  && r.endMonth().equals(endMonth));
    }
  }

  private static final class FakeIndexQueue implements IndexQueue {
    private final List<IndexMessage> enqueued = new ArrayList<>();

    List<IndexMessage> enqueued() {
      return enqueued;
    }

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
