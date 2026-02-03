package com.muchq.http_client.core;

import java.io.Closeable;

public interface HttpClient extends Closeable {
  HttpResponse execute(HttpRequest request);

  HttpResponse executeAsync(HttpRequest request);
}
