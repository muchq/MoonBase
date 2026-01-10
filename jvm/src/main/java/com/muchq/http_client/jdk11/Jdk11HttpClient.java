package com.muchq.http_client.jdk;

import com.muchq.http_client.core.HttpClient;
import com.muchq.http_client.core.HttpRequest;
import com.muchq.http_client.core.HttpResponse;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.util.Objects;

public class Jdk11HttpClient implements HttpClient {

    private final java.net.http.HttpClient delegate;

    public Jdk11HttpClient(java.net.http.HttpClient delegate) {
        this.delegate = Objects.requireNonNull(delegate);
    }

    @Override
    public HttpResponse execute(HttpRequest request) {
        java.net.http.HttpRequest httpRequest = toJdk11HttpRequest(request);
        java.net.http.HttpResponse<InputStream> response = null;
        try {
            response = delegate.send(httpRequest, java.net.http.HttpResponse.BodyHandlers.ofInputStream());
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(e);
        }
        return new Jdk11HttpResponse(request, response);
    }

    @Override
    public HttpResponse executeAsync(HttpRequest request) {
        throw new UnsupportedOperationException();
    }

    @Override
    public void close() {
        delegate.close();
    }

    private java.net.http.HttpRequest toJdk11HttpRequest(HttpRequest request) {
        var builder = java.net.http.HttpRequest.newBuilder().uri(request.getUrl());
        request.getHeaders().forEach(h -> builder.setHeader(h.getName(), h.getValue()));

        if (!request.getMethod().allowsBody() || request.getBody() == null) {
            builder.method(request.getMethod().name(), java.net.http.HttpRequest.BodyPublishers.noBody());
        } else {
            builder.method(request.getMethod().name(), java.net.http.HttpRequest.BodyPublishers.ofByteArray(request.getBody()));
        }

        return builder.build();
    }
}

