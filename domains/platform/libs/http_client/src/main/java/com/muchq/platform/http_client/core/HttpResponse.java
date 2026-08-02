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

  /**
   * @throws InterruptedRequestException if the calling thread is interrupted while the body is
   *     being read. The thread's interrupt status is set when it is thrown.
   */
  String getAsString();

  /**
   * @throws InterruptedRequestException if the calling thread is interrupted while the body is
   *     being read. The thread's interrupt status is set when it is thrown.
   */
  byte[] getAsBytes();

  /**
   * The body as a stream, for a caller that wants to hand it to a parser rather than buffer it.
   *
   * <p>Reading from it blocks, and the interrupt contract follows the bytes: a read interrupted
   * part-way through the body throws {@link InterruptedRequestException} from {@code read}, so a
   * caller that never touches this interface directly — a JSON parser draining the stream, say —
   * still surfaces the interrupt as one instead of as a truncated-response error.
   */
  InputStream getAsInputStream();
}
