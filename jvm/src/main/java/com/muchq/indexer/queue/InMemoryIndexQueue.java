package com.muchq.indexer.queue;

import java.time.Duration;
import java.util.Optional;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

public class InMemoryIndexQueue implements IndexQueue {
    private final LinkedBlockingQueue<IndexMessage> queue = new LinkedBlockingQueue<>();

    @Override
    public void enqueue(IndexMessage message) {
        queue.add(message);
    }

    @Override
    public Optional<IndexMessage> poll(Duration timeout) {
        try {
            IndexMessage msg = queue.poll(timeout.toMillis(), TimeUnit.MILLISECONDS);
            return Optional.ofNullable(msg);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return Optional.empty();
        }
    }

    @Override
    public int size() {
        return queue.size();
    }
}
