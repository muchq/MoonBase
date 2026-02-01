package com.muchq.indexer.queue;

import org.junit.Test;

import java.time.Duration;
import java.util.Optional;
import java.util.UUID;

import static org.assertj.core.api.Assertions.assertThat;

public class InMemoryIndexQueueTest {

    @Test
    public void testEnqueueAndPoll() {
        InMemoryIndexQueue queue = new InMemoryIndexQueue();
        IndexMessage message = new IndexMessage(UUID.randomUUID(), "hikaru", "chess.com", "2024-01", "2024-01");

        queue.enqueue(message);
        assertThat(queue.size()).isEqualTo(1);

        Optional<IndexMessage> polled = queue.poll(Duration.ofMillis(100));
        assertThat(polled).isPresent();
        assertThat(polled.get().player()).isEqualTo("hikaru");
        assertThat(queue.size()).isEqualTo(0);
    }

    @Test
    public void testPollEmpty() {
        InMemoryIndexQueue queue = new InMemoryIndexQueue();
        Optional<IndexMessage> polled = queue.poll(Duration.ofMillis(50));
        assertThat(polled).isEmpty();
    }

    @Test
    public void testSize() {
        InMemoryIndexQueue queue = new InMemoryIndexQueue();
        assertThat(queue.size()).isEqualTo(0);

        queue.enqueue(new IndexMessage(UUID.randomUUID(), "a", "chess.com", "2024-01", "2024-01"));
        queue.enqueue(new IndexMessage(UUID.randomUUID(), "b", "chess.com", "2024-01", "2024-01"));
        assertThat(queue.size()).isEqualTo(2);
    }
}
