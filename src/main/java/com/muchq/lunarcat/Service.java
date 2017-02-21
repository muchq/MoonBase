package com.muchq.lunarcat;


import com.google.inject.Binding;
import com.google.inject.Guice;
import com.google.inject.Injector;
import com.google.inject.Key;
import com.google.inject.Module;
import com.google.inject.Stage;
import com.google.inject.TypeLiteral;
import com.google.inject.name.Names;
import com.muchq.lunarcat.config.Configuration;
import com.muchq.lunarcat.config.LunarCatServiceModule;
import com.muchq.lunarcat.lifecycle.StartupTask;
import org.eclipse.jetty.server.Handler;
import org.eclipse.jetty.server.Server;
import org.eclipse.jetty.servlet.ServletContextHandler;
import org.eclipse.jetty.servlet.ServletHolder;
import org.jboss.resteasy.plugins.guice.GuiceResteasyBootstrapServletContextListener;
import org.jboss.resteasy.plugins.server.servlet.HttpServletDispatcher;

import java.util.EventListener;
import java.util.Set;

public class Service {
  private static final String DEFAULT_CONTEXT_PATH = "/";
  private static final String DEFAULT_SERVLET_PATH_SPEC = "/*";
  private static final int DEFAULT_PORT = 8080;

  private final Server server;
  private final Injector injector;

  public Service(Configuration configuration) {
    this.injector = createInjector(configuration);
    this.server = newServer(injector.getInstance(GuiceResteasyBootstrapServletContextListener.class));
  }

  public void run() {
    runStartupTasks();
    startHttp();
  }

  private void runStartupTasks() {
    injector.getInstance(Key.get(new TypeLiteral<Set<StartupTask>>(){}))
        .forEach(StartupTask::execute);
  }

  private void startHttp() {
    try {
      server.start();
      server.join();
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

  private Server newServer(EventListener listener) {
    Server server = new Server(getPort());
    server.setHandler(servletHandler(getAppRoot(), listener));
    return server;
  }

  private int getPort() {
    Binding<Integer> portBinding = injector.getExistingBinding(Key.get(Integer.class, Names.named("port")));

    if (portBinding != null) {
      return portBinding.getProvider().get();
    }
    return DEFAULT_PORT;
  }

  private String getAppRoot() {
    Binding<String> appRootBinding = injector.getExistingBinding(Key.get(String.class, Names.named("appRoot")));

    if (appRootBinding != null) {
      return appRootBinding.getProvider().get();
    }
    return DEFAULT_CONTEXT_PATH;
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
