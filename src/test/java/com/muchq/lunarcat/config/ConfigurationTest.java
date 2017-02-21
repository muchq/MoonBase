package com.muchq.lunarcat.config;

import com.google.inject.AbstractModule;
import com.google.inject.Module;
import org.junit.Test;

import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ThreadLocalRandom;

import static org.assertj.core.api.Assertions.assertThat;

public class ConfigurationTest {

  @Test
  public void itHasThePropertiesYouSet() {
    int port = ThreadLocalRandom.current().nextInt();
    String appRoot = UUID.randomUUID().toString();
    Module module = new AbstractModule() { protected void configure() {} };
    Configuration configuration = Configuration.newBuilder()
        .withBasePackage(getClass().getPackage())
        .withModules(module)
        .withAppRoot(appRoot)
        .withPort(port)
        .build();

    assertThat(configuration.getBasePackage()).isSameAs(getClass().getPackage());
    assertThat(configuration.getModules()).contains(module).hasSize(1);
    assertThat(configuration.getPort()).isEqualTo(port);
    assertThat(configuration.getContextPath()).isEqualTo(Optional.of(appRoot));
  }
}
