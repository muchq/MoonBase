package com.moonbase.smithy.runtime;

import java.util.HashMap;
import java.util.Map;

/**
 * Represents an HTTP request.
 */
public final class HttpRequest {
    private final String method;
    private final String path;
    private final String body;
    private final Map<String, String> headers;
    private final Map<String, String> queryParams;
    private final Map<String, String> pathParams;

    private HttpRequest(Builder builder) {
        this.method = builder.method;
        this.path = builder.path;
        this.body = builder.body;
        this.headers = Map.copyOf(builder.headers);
        this.queryParams = Map.copyOf(builder.queryParams);
        this.pathParams = Map.copyOf(builder.pathParams);
    }

    public String getMethod() {
        return method;
    }

    public String getPath() {
        return path;
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

    public Map<String, String> getQueryParams() {
        return queryParams;
    }

    public String getQueryParam(String name) {
        return queryParams.get(name);
    }

    public Map<String, String> getPathParams() {
        return pathParams;
    }

    public String getPathParam(String name) {
        return pathParams.get(name);
    }

    public static Builder builder() {
        return new Builder();
    }

    public static final class Builder {
        private String method = "GET";
        private String path = "/";
        private String body = "";
        private final Map<String, String> headers = new HashMap<>();
        private final Map<String, String> queryParams = new HashMap<>();
        private final Map<String, String> pathParams = new HashMap<>();

        public Builder method(String method) {
            this.method = method;
            return this;
        }

        public Builder path(String path) {
            this.path = path;
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

        public Builder queryParam(String name, String value) {
            this.queryParams.put(name, value);
            return this;
        }

        public Builder pathParam(String name, String value) {
            this.pathParams.put(name, value);
            return this;
        }

        public HttpRequest build() {
            return new HttpRequest(this);
        }
    }
}
