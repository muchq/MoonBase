package com.muchq.platform.http_client.core;

import java.io.InputStream;
import java.util.List;

public interface HttpResponse {
  HttpRequest getRequest();

  int getStatusCode();

  boolean isSuccess();

  boolean isError();

  boolean isClientError();

  boolean isServerError();

  List<Header> getHeaders();

  String getAsString();

  byte[] getAsBytes();

  InputStream getAsInputStream();
}
