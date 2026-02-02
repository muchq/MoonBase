package com.muchq.one_d4.worker;

import com.muchq.one_d4.queue.IndexMessage;
import com.muchq.one_d4.queue.IndexQueue;
import io.micronaut.context.event.ApplicationEventListener;
import io.micronaut.runtime.server.event.ServerStartupEvent;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Duration;
import java.util.Optional;

public class IndexWorkerLifecycle implements ApplicationEventListener<ServerStartupEvent> {
    private static final Logger LOG = LoggerFactory.getLogger(IndexWorkerLifecycle.class);

    private final IndexQueue queue;
    private final IndexWorker worker;
    private volatile boolean running = true;

    public IndexWorkerLifecycle(IndexQueue queue, IndexWorker worker) {
        this.queue = queue;
        this.worker = worker;
    }

    @Override
    public void onApplicationEvent(ServerStartupEvent event) {
        Thread workerThread = new Thread(this::pollLoop, "index-worker");
        workerThread.setDaemon(true);
        workerThread.start();
        LOG.info("Index worker started");
    }

    private void pollLoop() {
        while (running) {
            try {
                Optional<IndexMessage> message = queue.poll(Duration.ofSeconds(5));
                message.ifPresent(worker::process);
            } catch (Exception e) {
                LOG.error("Error in index worker poll loop", e);
            }
        }
    }

    public void stop() {
        running = false;
    }
}
