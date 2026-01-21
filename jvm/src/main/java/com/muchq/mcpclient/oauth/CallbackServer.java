package com.muchq.mcpclient.oauth;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Temporary HTTP server for OAuth callback.
 *
 * This server:
 * 1. Listens on localhost for the OAuth redirect
 * 2. Parses the authorization code from the query string
 * 3. Returns a success/error HTML page to the browser
 * 4. Provides the authorization code to the OAuth flow
 *
 * Security: Only accepts connections on localhost to prevent external access.
 */
public class CallbackServer {

    private static final Logger LOG = LoggerFactory.getLogger(CallbackServer.class);

    private final HttpServer server;
    private final CompletableFuture<AuthorizationResponse> responseFuture;
    private final int port;
    private final String expectedState;

    /**
     * Creates a new callback server listening on the specified port.
     *
     * @param port Port to listen on (typically 8888)
     * @throws IOException if the server cannot be started
     */
    public CallbackServer(int port, String expectedState) throws IOException {
        this.port = port;
        this.expectedState = expectedState;
        this.responseFuture = new CompletableFuture<>();

        // Create server bound to localhost only
        this.server = HttpServer.create(new InetSocketAddress("localhost", port), 0);

        // Register callback endpoint
        server.createContext("/callback", this::handleCallback);

        LOG.info("OAuth callback server created on port {}", port);
    }

    /**
     * Starts the callback server.
     */
    public void start() {
        server.start();
        LOG.info("OAuth callback server started: http://localhost:{}/callback", port);
    }

    /**
     * Stops the callback server.
     */
    public void stop() {
        server.stop(0);
        LOG.info("OAuth callback server stopped");
    }

    /**
     * Waits for the OAuth authorization response.
     *
     * @param timeout Maximum time to wait
     * @param unit Time unit for timeout
     * @return Authorization code if successful
     * @throws TimeoutException if timeout is reached
     * @throws IOException if authorization failed or was denied
     * @throws InterruptedException if interrupted while waiting
     */
    public String waitForAuthorizationCode(long timeout, TimeUnit unit)
        throws TimeoutException, IOException, InterruptedException {
        try {
            AuthorizationResponse response = responseFuture.get(timeout, unit);

            if (response.isSuccess()) {
                return response.code();
            } else {
                throw new IOException("Authorization failed: " + response.error());
            }
        } catch (java.util.concurrent.ExecutionException e) {
            throw new IOException("Authorization error", e.getCause());
        }
    }

    /**
     * Handles the OAuth callback request.
     */
    private void handleCallback(HttpExchange exchange) throws IOException {
        try {
            URI requestUri = exchange.getRequestURI();
            Map<String, String> queryParams = parseQueryString(requestUri.getQuery());

            LOG.debug("Received OAuth callback: {}", queryParams.keySet());

            if (expectedState != null) {
                String receivedState = queryParams.get("state");
                if (receivedState == null || !expectedState.equals(receivedState)) {
                    LOG.warn("Invalid OAuth state: expected={}, received={}", expectedState, receivedState);
                    responseFuture.complete(AuthorizationResponse.error("invalid_state", "State mismatch"));
                    sendErrorResponse(exchange, "invalid_state", "State mismatch");
                    return;
                }
            }

            if (queryParams.containsKey("code")) {
                // Success - authorization code received
                String code = queryParams.get("code");
                LOG.info("Authorization code received");

                responseFuture.complete(AuthorizationResponse.success(code));
                try {
                    sendSuccessResponse(exchange);
                } catch (IOException e) {
                    LOG.debug("Failed to write success response to browser", e);
                }

            } else if (queryParams.containsKey("error")) {
                // Error - authorization failed/denied
                String error = queryParams.get("error");
                String errorDescription = queryParams.getOrDefault("error_description", "Unknown error");
                LOG.warn("Authorization failed: {} - {}", error, errorDescription);

                responseFuture.complete(AuthorizationResponse.error(error, errorDescription));
                try {
                    sendErrorResponse(exchange, error, errorDescription);
                } catch (IOException e) {
                    LOG.debug("Failed to write error response to browser", e);
                }

            } else {
                // Invalid callback - missing both code and error
                LOG.warn("Invalid OAuth callback: missing code and error parameters");
                responseFuture.completeExceptionally(
                    new IOException("Invalid OAuth callback: missing code and error")
                );
                try {
                    sendErrorResponse(exchange, "invalid_request", "Missing authorization code");
                } catch (IOException e) {
                    LOG.debug("Failed to write error response to browser", e);
                }
            }

        } catch (Exception e) {
            LOG.error("Error handling OAuth callback", e);
            responseFuture.completeExceptionally(e);
            try {
                sendErrorResponse(exchange, "server_error", "Internal server error");
            } catch (IOException ioException) {
                LOG.debug("Failed to write error response to browser", ioException);
            }
        }
    }

    /**
     * Parses query string into key-value map.
     * Handles parameters with and without values (e.g., "foo=bar" and "foo").
     */
    private Map<String, String> parseQueryString(String query) {
        Map<String, String> params = new HashMap<>();
        if (query == null || query.isEmpty()) {
            return params;
        }

        for (String param : query.split("&")) {
            if (param.isEmpty()) {
                continue;
            }
            String[] pair = param.split("=", 2);
            String key = urlDecode(pair[0]);
            String value = pair.length == 2 ? urlDecode(pair[1]) : "";
            params.put(key, value);
        }

        return params;
    }

    private String urlDecode(String value) {
        try {
            return java.net.URLDecoder.decode(value, StandardCharsets.UTF_8);
        } catch (IllegalArgumentException e) {
            return value;
        }
    }

    /**
     * Sends success HTML response to browser.
     */
    private void sendSuccessResponse(HttpExchange exchange) throws IOException {
        String html = """
            <!DOCTYPE html>
            <html>
            <head>
                <title>Authorization Successful</title>
                <style>
                    body {
                        font-family: system-ui, -apple-system, sans-serif;
                        display: flex;
                        justify-content: center;
                        align-items: center;
                        height: 100vh;
                        margin: 0;
                        background: #f0f0f0;
                    }
                    .container {
                        background: white;
                        padding: 40px;
                        border-radius: 8px;
                        box-shadow: 0 2px 10px rgba(0,0,0,0.1);
                        text-align: center;
                    }
                    h1 { color: #22c55e; margin: 0 0 20px 0; }
                    p { color: #666; margin: 0; }
                </style>
            </head>
            <body>
                <div class="container">
                    <h1>Authorization Successful!</h1>
                    <p>You can close this window and return to the application.</p>
                </div>
            </body>
            </html>
            """;

        sendHtmlResponse(exchange, 200, html);
    }

    /**
     * Sends error HTML response to browser.
     */
    private void sendErrorResponse(HttpExchange exchange, String error, String description) throws IOException {
        String html = String.format("""
            <!DOCTYPE html>
            <html>
            <head>
                <title>Authorization Failed</title>
                <style>
                    body {
                        font-family: system-ui, -apple-system, sans-serif;
                        display: flex;
                        justify-content: center;
                        align-items: center;
                        height: 100vh;
                        margin: 0;
                        background: #f0f0f0;
                    }
                    .container {
                        background: white;
                        padding: 40px;
                        border-radius: 8px;
                        box-shadow: 0 2px 10px rgba(0,0,0,0.1);
                        text-align: center;
                    }
                    h1 { color: #ef4444; margin: 0 0 20px 0; }
                    p { color: #666; margin: 10px 0; }
                    code { background: #f5f5f5; padding: 2px 6px; border-radius: 3px; }
                </style>
            </head>
            <body>
                <div class="container">
                    <h1>Authorization Failed</h1>
                    <p><code>%s</code></p>
                    <p>%s</p>
                </div>
            </body>
            </html>
            """, error, description);

        sendHtmlResponse(exchange, 200, html);
    }

    /**
     * Sends HTML response.
     */
    private void sendHtmlResponse(HttpExchange exchange, int statusCode, String html) throws IOException {
        byte[] bytes = html.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().set("Content-Type", "text/html; charset=UTF-8");
        exchange.sendResponseHeaders(statusCode, bytes.length);

        try (OutputStream os = exchange.getResponseBody()) {
            os.write(bytes);
        }
    }

    /**
     * Authorization response from OAuth callback.
     */
    private record AuthorizationResponse(boolean success, String code, String error, String errorDescription) {

        static AuthorizationResponse success(String code) {
            return new AuthorizationResponse(true, code, null, null);
        }

        static AuthorizationResponse error(String error, String errorDescription) {
            return new AuthorizationResponse(false, null, error, errorDescription);
        }

        boolean isSuccess() {
            return success;
        }
    }
}
