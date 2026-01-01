package com.muchq.helloworld;

import org.eclipse.jetty.server.Server;
import org.eclipse.jetty.server.handler.AbstractHandler;
import org.eclipse.jetty.server.Request;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import java.io.IOException;
import java.io.PrintWriter;

public class App {
  private static final Logger logger = LoggerFactory.getLogger(App.class);

  public static void main(String[] args) throws Exception {
    int port = Integer.parseInt(System.getenv().getOrDefault("PORT", "8080"));

    logger.info("Starting hello-world HTTP server on port {}", port);

    Server server = new Server(port);
    server.setHandler(new HelloWorldHandler());

    server.start();
    logger.info("Server started successfully on port {}", port);
    server.join();
  }

  private static class HelloWorldHandler extends AbstractHandler {
    @Override
    public void handle(String target, Request baseRequest, HttpServletRequest request,
                       HttpServletResponse response) throws IOException, ServletException {
      response.setContentType("application/json;charset=utf-8");
      response.setStatus(HttpServletResponse.SC_OK);
      baseRequest.setHandled(true);

      PrintWriter out = response.getWriter();
      out.println("{");
      out.println("  \"message\": \"Hello, World!\",");
      out.println("  \"path\": \"" + target + "\",");
      out.println("  \"method\": \"" + request.getMethod() + "\"");
      out.println("}");

      logger.info("{} {}", request.getMethod(), target);
    }
  }
}
