package com.muchq.lunarcat.config;

import com.google.common.base.Preconditions;
import com.google.inject.Module;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Optional;
import java.util.Set;

public class Configuration {
  public static final String PORT_PROPERTY_NAME = "UNSET_ENV_SERVICE_PORT";
  public static final String CONTEXT_PATH_PROPERTY_NAME = "UNSET_ENV_SERVICE_CONTEXT_PATH";

  private final Integer port;
  private final Package basePackage;
  private Optional<String> contextPathMaybe = Optional.empty();
  private final Set<Module> modules = new HashSet<>();

  private Configuration(Integer port,
                        Package basePackage,
                        String contextPathMaybe,
                        Set<Module> modules) {
    this.port = port;
    this.basePackage = basePackage;
    this.contextPathMaybe = Optional.ofNullable(contextPathMaybe);
    this.modules.addAll(modules);
  }

  public int getPort() {
    return port;
  }

  public Package getBasePackage() {
    return basePackage;
  }

  public Optional<String> getContextPath() {
    return contextPathMaybe;
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
      Integer port = parsePropertyAsInteger(PORT_PROPERTY_NAME);
      String appRoot = normalizeAppRoot(findProperty(CONTEXT_PATH_PROPERTY_NAME));
      return new Configuration(port, basePackage, appRoot, modules);
    }
  }

  private static String normalizeAppRoot(String rawAppRoot) {
    if (rawAppRoot == null || "/".equals(rawAppRoot)) {
      return "/";
    }

    if (rawAppRoot.startsWith("/")) {
      return rawAppRoot;
    }

    return "/" + rawAppRoot;
  }

  private static String findProperty(String propertyName) {
    String property = System.getProperty(propertyName);
    if (property == null) {
      property = System.getenv(propertyName);
    }
    if (property == null) {
      throw new RuntimeException("Couldn't find property: " + propertyName);
    }
    return property;
  }

  private static Integer parsePropertyAsInteger(String propertyName) {
    try {
      return Integer.parseInt(findProperty(propertyName));
    } catch (NumberFormatException n) {
      throw new RuntimeException("Failed to parse '" + propertyName + "' as an integer");
    }
  }
}
