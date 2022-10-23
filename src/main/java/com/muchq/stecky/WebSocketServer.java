package com.muchq.stecky;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Guice;
import com.google.inject.Injector;
import com.google.inject.Module;
import com.google.inject.Stage;
import org.eclipse.jetty.server.Server;
import org.eclipse.jetty.server.ServerConnector;
import org.eclipse.jetty.servlet.ServletContextHandler;
import org.eclipse.jetty.websocket.jsr356.server.deploy.WebSocketServerContainerInitializer;

import javax.servlet.ServletException;
import javax.websocket.DeploymentException;
import javax.websocket.server.ServerContainer;
import javax.websocket.server.ServerEndpointConfig;
import java.util.Objects;
import java.util.Set;

public class WebSocketServer {
  private final Injector injector;
  private final Server server;

  private WebSocketServer(Injector injector, Server server) {
    this.injector = injector;
    this.server = server;
  }

  public Injector getInjector() {
    return injector;
  }

  public void start() {
    startAndDontBlock();
    try {
      server.join();
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
      throw new RuntimeException(e);
    }
  }

  public void startAndDontBlock() {
    try {
      server.start();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public void stop() {
    try {
      server.stop();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public static Builder newBuilder() {
    return new Builder().port(getPort());
  }

  public static class Builder {
    private Set<Module> modules;
    private int port = 0;
    private Set<Mapping> mappings;

    public Builder addMappings(Mapping... mappings) {
      this.mappings = ImmutableSet.copyOf(mappings);
      return this;
    }

    public Builder addModules(Module... modules) {
      this.modules = ImmutableSet.<Module>builder()
          .addAll(ImmutableSet.copyOf(modules))
          .add(new WebSocketModule())
          .build();
      return this;
    }

    public Builder port(int port) {
      this.port = port;
      return this;
    }

    public WebSocketServer build() {
      Objects.requireNonNull(modules, "modules");
      Objects.requireNonNull(mappings, "mappings");

      Injector injector = Guice.createInjector(Stage.PRODUCTION, modules);

      Server server = new Server();
      ServerConnector connector = new ServerConnector(server);
      connector.setPort(port);
      server.addConnector(connector);

      ServletContextHandler context = new ServletContextHandler(ServletContextHandler.SESSIONS);
      context.setContextPath("/");
      server.setHandler(context);

      // Initialize javax.websocket layer
      final ServerContainer wscontainer;
      try {
        wscontainer = WebSocketServerContainerInitializer.configureContext(context);
      } catch (ServletException e) {
        throw new RuntimeException(e);
      }

      for (Mapping mapping : mappings) {
        try {
          wscontainer.addEndpoint(
              ServerEndpointConfig.Builder.create(mapping.getHandler(), mapping.getPath())
                  .configurator(injector.getInstance(GuiceConfigurator.class))
                  .build()
          );
        } catch (DeploymentException e) {
          throw new RuntimeException(e);
        }
      }

      return new WebSocketServer(injector, server);
    }

    public void buildAndStart() {
      try {
        build().start();
      } catch (Exception e) {
        throw new RuntimeException(e);
      }
    }
  }

  private static int getPort() {
    final String portFromProp = System.getProperty("port");

    if (portFromProp == null) {
      return 8080;
    }

    return Integer.parseInt(portFromProp);
  }
}
