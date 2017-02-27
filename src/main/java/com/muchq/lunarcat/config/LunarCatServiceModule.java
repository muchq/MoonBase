package com.muchq.lunarcat.config;

import com.google.common.collect.Sets;
import com.google.inject.AbstractModule;
import com.google.inject.multibindings.Multibinder;
import com.muchq.json.ObjectMapperModule;
import com.muchq.lunarcat.lifecycle.StartupTask;
import org.jboss.resteasy.plugins.guice.ext.RequestScopeModule;
import org.reflections.Reflections;
import org.reflections.scanners.SubTypesScanner;
import org.reflections.util.ConfigurationBuilder;

import javax.ws.rs.Path;
import javax.ws.rs.ext.Provider;
import java.util.Set;

public class LunarCatServiceModule extends AbstractModule {
  private final Set<String> packagesToScan = Sets.newHashSet("com.muchq.lunarcat");

  public LunarCatServiceModule(String basePackage) {
    this.packagesToScan.add(basePackage);
  }

  @Override
  protected void configure() {
    install(new RequestScopeModule());
    install(new ObjectMapperModule());
    packagesToScan.forEach(this::bindJaxRs);
    bindLifeCycle(Multibinder.newSetBinder(binder(), StartupTask.class));
  }

  private void bindLifeCycle(Multibinder<StartupTask> multibinder) {
    Set<Class<? extends StartupTask>> tasks =
        new Reflections(
            new ConfigurationBuilder()
                .forPackages(packagesToScan.toArray(new String[packagesToScan.size()]))
                .setScanners(new SubTypesScanner(true)))
            .getSubTypesOf(StartupTask.class);

    if (tasks != null) {
      for (Class<? extends StartupTask> task : tasks) {
        multibinder.addBinding().to(task);
      }
    }
  }


  private void bindJaxRs(String packageName) {
    Reflections reflections = new Reflections(packageName);
    reflections.getTypesAnnotatedWith(Path.class).forEach(this::bind);
    reflections.getTypesAnnotatedWith(Provider.class).forEach(this::bind);
  }
}
