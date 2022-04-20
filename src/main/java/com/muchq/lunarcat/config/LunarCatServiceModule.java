package com.muchq.lunarcat.config;

import com.google.common.collect.Sets;
import com.google.inject.AbstractModule;
import com.google.inject.Inject;
import com.google.inject.Scope;
import com.google.inject.Scopes;
import com.google.inject.Singleton;
import com.google.inject.multibindings.Multibinder;
import com.google.inject.name.Named;
import com.muchq.json.ObjectMapperModule;
import com.muchq.lunarcat.lifecycle.StartupTask;
import java.lang.annotation.Annotation;
import java.util.Set;
import javax.ws.rs.Path;
import javax.ws.rs.ext.Provider;
import org.jboss.resteasy.plugins.guice.ext.RequestScopeModule;
import org.reflections.Reflections;
import org.reflections.scanners.SubTypesScanner;
import org.reflections.util.ConfigurationBuilder;

public class LunarCatServiceModule extends AbstractModule {

  private final Set<String> packagesToScan = Sets.newHashSet("com.muchq.lunarcat");

  public LunarCatServiceModule(String basePackage) {
    this.packagesToScan.add(basePackage);
  }

  @Override
  protected void configure() {
    install(new RequestScopeModule());
    install(new ObjectMapperModule());
    packagesToScan.forEach(this::bindPackage);
    bindStartupTasks();
  }

  private void bindStartupTasks() {
    Multibinder<StartupTask> multibinder = Multibinder.newSetBinder(binder(), StartupTask.class);
    Set<Class<? extends StartupTask>> tasks = new Reflections(
      new ConfigurationBuilder()
        .forPackages(packagesToScan.toArray(new String[packagesToScan.size()]))
        .setScanners(new SubTypesScanner(true))
    )
      .getSubTypesOf(StartupTask.class);

    if (tasks != null) {
      for (Class<? extends StartupTask> task : tasks) {
        multibinder.addBinding().to(task);
      }
    }
  }

  private void bindPackage(String packageName) {
    Reflections reflections = new Reflections(packageName);
    bindJaxRsTypes(reflections);
    bindInjectTypes(reflections);
  }

  private void bindJaxRsTypes(Reflections reflections) {
    bindType(reflections, Path.class);
    bindType(reflections, Provider.class);
  }

  private void bindInjectTypes(Reflections reflections) {
    bindType(reflections, Inject.class);
    bindType(reflections, javax.inject.Inject.class);

    bindType(reflections, Named.class);
    bindType(reflections, javax.inject.Named.class);

    bindSingleton(reflections, Singleton.class, Scopes.SINGLETON);
    bindSingleton(reflections, javax.inject.Singleton.class, Scopes.SINGLETON);
  }

  private void bindType(Reflections reflections, Class<? extends Annotation> type) {
    reflections.getTypesAnnotatedWith(type).forEach(this::bind);
  }

  private void bindSingleton(Reflections reflections, Class<? extends Annotation> type, Scope scope) {
    reflections.getTypesAnnotatedWith(type).forEach(this::bindSingleton);
  }

  private <T> void bindSingleton(Class<T> type) {
    bind(type).in(Scopes.SINGLETON);
  }
}
