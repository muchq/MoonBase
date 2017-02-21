package com.muchq.lunarcat.config;

import com.google.common.base.Preconditions;
import com.google.inject.Module;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

public class Configuration {
  private final Package basePackage;
  private final Set<Module> modules = new HashSet<>();

  private Configuration(Package basePackage,
                        Set<Module> modules) {
    this.basePackage = basePackage;
    this.modules.addAll(modules);
  }

  public Package getBasePackage() {
    return basePackage;
  }

  public Set<Module> getModules() {
    return modules;
  }

  public static Builder newBuilder() {
    return new Builder();
  }

  public static class Builder {
    private Package basePackage;
    private final Set<Module> modules = new HashSet<>();

    public Builder withBasePackage(Package basePackage) {
      this.basePackage = basePackage;
      return this;
    }

    public Builder withModules(Module... modules) {
      if (modules != null) {
        this.modules.addAll(Arrays.asList(modules));
      }
      return this;
    }

    public Configuration build() {
      Preconditions.checkArgument(basePackage != null, "basePackage may not be null;");
      return new Configuration(basePackage, modules);
    }
  }
}
