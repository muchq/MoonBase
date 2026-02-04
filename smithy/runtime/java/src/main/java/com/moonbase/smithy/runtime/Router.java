package com.moonbase.smithy.runtime;

import java.util.concurrent.CompletableFuture;

/**
 * Interface for HTTP request routing.
 */
public interface Router {
    /**
     * Routes an HTTP request to the appropriate handler.
     *
     * @param request The incoming HTTP request
     * @return A future containing the HTTP response
     */
    CompletableFuture<HttpResponse> route(HttpRequest request);
}
