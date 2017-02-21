package com.muchq.lunarcat.config;

import com.google.inject.AbstractModule;
import com.google.inject.Module;
import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThat;

public class ConfigurationTest {

  @Test
  public void itHasThePropertiesYouSet() {
    Module module = new AbstractModule() { protected void configure() {} };
    Configuration configuration = Configuration.newBuilder()
        .withBasePackage(getClass().getPackage())
        .withModules(module)
        .build();

    assertThat(configuration.getBasePackage()).isSameAs(getClass().getPackage());
    assertThat(configuration.getModules()).contains(module).hasSize(1);
  }
}
