# HTTP Client

A simple, type-safe HTTP client library for Java.

## Overview

This library provides a clean abstraction over HTTP operations with a builder-based API for constructing requests and handling responses.

## Quick Start

### 1. Create a client

```java
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;

HttpClient client = new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient());
```

### 2. Build a request

```java
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpRequest.Method;
import com.muchq.platform.http_client.core.HttpRequest.ContentType;

// Simple GET request
HttpRequest getRequest = HttpRequest.newBuilder()
        .setUrl("https://api.example.com/users")
        .build();

        // POST request with JSON body
        HttpRequest postRequest = HttpRequest.newBuilder()
                .setUrl("https://api.example.com/users")
                .setMethod(Method.POST)
                .setContentType(ContentType.JSON)
                .setBody("{\"name\": \"John\"}")
                .build();

        // Request with custom headers
        HttpRequest customRequest = HttpRequest.newBuilder()
                .setUrl("https://api.example.com/data")
                .addHeader("Authorization", "Bearer token123")
                .setAccept(ContentType.JSON)
                .build();
```

### 3. Execute the request

```java
HttpResponse response = client.execute(request);
```

### 4. Handle the response

```java
// Check status
if (response.isSuccess()) {
    String body = response.getAsString();
    // or
    byte[] bytes = response.getAsBytes();
    // or
    InputStream stream = response.getAsInputStream();
}

if (response.isClientError()) {
    System.err.println("Client error: " + response.getStatusCode());
}
```

## Complete Example

```java
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;

try(HttpClient client = new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient())){
HttpRequest request = HttpRequest.newBuilder()
        .setUrl("https://api.example.com/data")
        .build();

HttpResponse response = client.execute(request);

    if(response.

isSuccess()){
        System.out.

println(response.getAsString());
        }else{
        System.err.

println("Request failed: "+response.getStatusCode());
        }
        }
```

## Interruption

Nothing bounds a call by default. No request timeout is set, and a peer that accepts the
connection and then goes quiet produces no bytes, no error and no EOF — so a thread parked in
`execute`, or in a read of the response body, stays there. `Thread.interrupt()` is the only way to
get it back.

Both blocking points honour it — the underlying JDK client always did — and both report it as
`InterruptedRequestException` rather than leaving you to identify it. That is the part this library
adds, and it is worth being precise about what it is and isn't.

It is **not** recovering information that was lost. The JDK leaves the interrupt identifiable:
`send` throws `InterruptedException`, and a body read throws `IOException(InterruptedException)`
with the thread's interrupt status set. A caller could walk that chain.

What the type gives you is (a) not having to, against a convention nothing documents — the
`IOException` wrapping is a detail of `HttpResponseInputStream`, not of `BodyHandlers`' contract —
and (b) precision. The cheap alternative is checking `Thread.currentThread().isInterrupted()` after
catching, and that answers a question about the *thread* when you asked about the *call*: a
genuinely truncated body, on a thread interrupted for some unrelated reason, reads as a
cancellation that never happened.

```java
Thread worker = Thread.ofVirtual().start(() -> {
    try {
        HttpResponse response = client.execute(request);
        process(response.getAsInputStream());   // body reads throw it too
    } catch (InterruptedRequestException e) {
        // Told to stop. Not a failure of the request — unwind quietly.
    }
});

worker.interrupt();   // returns promptly, however silent the peer is
```

Three things to know:

- **The interrupt status is always set** when the exception is thrown, so a caller that catches it
  and keeps going still sees the interrupt at its next blocking point. If you are the one who sent
  the interrupt and the thread has more work to do afterwards, consume it with
  `Thread.interrupted()` once the call has unwound.
- **A thread that is already interrupted does not get parked**, so unwinding through a loop of
  calls does not cost one unbounded wait per remaining iteration.
- **The exception reaches parsers too.** `getAsInputStream()` is wrapped, so an interrupt during a
  body read comes out of `read` — a Jackson `readValue` over the stream propagates it rather than
  turning it into a parse failure.

Ordinary transport failures are unaffected: a refused connection is still an `UncheckedIOException`
and a truncated body is still an I/O error, on a thread whose interrupt status is untouched.

## API Reference

### HttpRequest.Builder

- `setUrl(String url)` - Set the request URL (required)
- `setMethod(Method method)` - Set HTTP method (defaults to GET)
- `setBody(String body)` or `setBody(byte[] body)` - Set request body
- `setContentType(ContentType contentType)` - Set Content-Type header
- `setAccept(ContentType accept)` - Set Accept header
- `addHeader(String name, String value)` - Add custom header
- `build()` - Build the HttpRequest

### Supported Methods

- `GET`, `POST`, `PUT`, `DELETE`, `PATCH`, `HEAD`

### Content Types

- `TEXT`, `JSON`, `XML`, `PROTOBUF`, `FORM`, `CSV`, `OCTET_STREAM`

### HttpResponse

- `getStatusCode()` - Get HTTP status code
- `isSuccess()` - Returns true for 2xx status codes
- `isError()` - Returns true for 4xx or 5xx status codes
- `isClientError()` - Returns true for 4xx status codes
- `isServerError()` - Returns true for 5xx status codes
- `getAsString()` - Get response body as string
- `getAsBytes()` - Get response body as byte array
- `getAsInputStream()` - Get response body as input stream
- `getHeaders()` - Get response headers
- `getRequest()` - Get the original request

The three body accessors block, and all three throw `InterruptedRequestException` if the calling
thread is interrupted while they do. See [Interruption](#interruption).
