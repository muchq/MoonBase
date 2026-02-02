package com.muchq.one_d4.queue;

import java.time.Duration;
import java.util.Optional;

public interface IndexQueue {
    void enqueue(IndexMessage message);
    Optional<IndexMessage> poll(Duration timeout);
    int size();
}
