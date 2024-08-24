package com.muchq.cards.golf_ws;

import org.eclipse.jetty.server.Server;
import org.eclipse.jetty.servlet.ServletContextHandler;
import org.eclipse.jetty.servlet.ServletHolder;
import org.eclipse.jetty.websocket.server.config.JettyWebSocketServletContainerInitializer;

public class Main {
  public static void main(String[] args) throws Exception {
    Server server = new Server(8080);

    ServletContextHandler context = new ServletContextHandler(ServletContextHandler.SESSIONS);
    context.setContextPath("/");
    server.setHandler(context);

    JettyWebSocketServletContainerInitializer.configure(
        context,
        (servletContext, wsContainer) -> {
          wsContainer.addMapping("", (req, res) -> new GameHandler());
        });

    ServletHolder holder = new ServletHolder(new org.eclipse.jetty.servlet.DefaultServlet());
    context.addServlet(holder, "/");

    server.start();
    server.join();
  }
}
