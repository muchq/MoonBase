package com.muchq.lunarcat;


import com.google.inject.Guice;
import com.google.inject.Injector;
import com.google.inject.Key;
import com.google.inject.Module;
import com.google.inject.Stage;
import com.google.inject.TypeLiteral;
import com.muchq.lunarcat.config.Configuration;
import com.muchq.lunarcat.config.LunarCatServiceModule;
import com.muchq.lunarcat.lifecycle.StartupTask;
import org.eclipse.jetty.server.Handler;
import org.eclipse.jetty.server.Server;
import org.eclipse.jetty.servlet.ServletContextHandler;
import org.eclipse.jetty.servlet.ServletHolder;
import org.jboss.resteasy.plugins.guice.GuiceResteasyBootstrapServletContextListener;
import org.jboss.resteasy.plugins.server.servlet.HttpServletDispatcher;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.EventListener;
import java.util.Set;

public class Service {
  private static final Logger LOGGER = LoggerFactory.getLogger(Service.class);
  private static final String DEFAULT_CONTEXT_PATH = "/";
  private static final String DEFAULT_SERVLET_PATH_SPEC = "/*";
  private static final TypeLiteral<Set<StartupTask>> TASKS_TYPE = new TypeLiteral<Set<StartupTask>>(){};

  private enum ServerMode { WAIT, NO_WAIT }

  private final Server server;
  private final Injector injector;

  public Service(Configuration configuration) {
    this.injector = createInjector(configuration);
    this.server = newServer(configuration,
                            injector.getInstance(
                                GuiceResteasyBootstrapServletContextListener.class));
  }

  public void run() {
    runStartupTasks();
    startHttp(ServerMode.WAIT);
  }

  void runNoWait() {
    runStartupTasks();
    startHttp(ServerMode.NO_WAIT);
  }

  public void shutDown() {
    try {
      server.stop();
    } catch (Exception e) {
      LOGGER.error("failed to stop server due to {}", e.getMessage(), e);
      throw new RuntimeException(e);
    }
  }

  private void runStartupTasks() {
    injector.getInstance(Key.get(TASKS_TYPE)).forEach(StartupTask::execute);
  }

  private void startHttp(ServerMode mode) {
    try {
      server.start();
      if (mode == ServerMode.WAIT) {
        server.join();
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private Injector createInjector(Configuration configuration) {
    Set<Module> modules = configuration.getModules();
    modules.add(new LunarCatServiceModule(configuration.getBasePackage().getName()));

    Injector injector = Guice.createInjector(Stage.PRODUCTION, modules);
    injector.getAllBindings();
    injector.createChildInjector().getAllBindings();
    return injector;
  }

  private Server newServer(Configuration configuration, EventListener listener) {
    Server server = new Server(configuration.getPort());
    server.setHandler(servletHandler(configuration.getContextPath().orElse(DEFAULT_CONTEXT_PATH), listener));
    return server;
  }

  private Handler servletHandler(String contextPath, EventListener listener) {
    ServletContextHandler servletHandler = new ServletContextHandler();
    servletHandler.addEventListener(listener);

    ServletHolder servletHolder = new ServletHolder(HttpServletDispatcher.class);
    servletHandler.addServlet(servletHolder, DEFAULT_SERVLET_PATH_SPEC);
    servletHandler.setContextPath(contextPath);
    return servletHandler;
  }
}
