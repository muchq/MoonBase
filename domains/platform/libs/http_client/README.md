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
