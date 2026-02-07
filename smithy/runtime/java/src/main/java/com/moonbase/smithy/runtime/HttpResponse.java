package com.moonbase.smithy.runtime;

import java.util.HashMap;
import java.util.Map;

/**
 * Represents an HTTP response.
 */
public final class HttpResponse {
    private final int statusCode;
    private final String body;
    private final Map<String, String> headers;

    private HttpResponse(int statusCode, String body, Map<String, String> headers) {
        this.statusCode = statusCode;
        this.body = body;
        this.headers = Map.copyOf(headers);
    }

    public int getStatusCode() {
        return statusCode;
    }

    public String getBody() {
        return body;
    }

    public Map<String, String> getHeaders() {
        return headers;
    }

    public String getHeader(String name) {
        return headers.get(name);
    }

    public boolean isSuccessful() {
        return statusCode >= 200 && statusCode < 300;
    }

    public static HttpResponse ok(String body) {
        return new HttpResponse(200, body, Map.of("Content-Type", "application/json"));
    }

    public static HttpResponse created(String body) {
        return new HttpResponse(201, body, Map.of("Content-Type", "application/json"));
    }

    public static HttpResponse noContent() {
        return new HttpResponse(204, "", Map.of());
    }

    public static HttpResponse badRequest(String body) {
        return new HttpResponse(400, body, Map.of("Content-Type", "application/json"));
    }

    public static HttpResponse notFound() {
        return new HttpResponse(404, "{\"error\":\"Not Found\"}", Map.of("Content-Type", "application/json"));
    }

    public static HttpResponse serverError(String body) {
        return new HttpResponse(500, body, Map.of("Content-Type", "application/json"));
    }

    public static HttpResponse of(int statusCode, String body) {
        return new HttpResponse(statusCode, body, Map.of("Content-Type", "application/json"));
    }

    public static Builder builder() {
        return new Builder();
    }

    public static final class Builder {
        private int statusCode = 200;
        private String body = "";
        private final Map<String, String> headers = new HashMap<>();

        public Builder statusCode(int statusCode) {
            this.statusCode = statusCode;
            return this;
        }

        public Builder body(String body) {
            this.body = body;
            return this;
        }

        public Builder header(String name, String value) {
            this.headers.put(name, value);
            return this;
        }

        public HttpResponse build() {
            return new HttpResponse(statusCode, body, headers);
        }
    }
}
