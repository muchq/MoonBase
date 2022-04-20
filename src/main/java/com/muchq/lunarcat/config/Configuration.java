package com.muchq.lunarcat.config;

import com.google.common.base.Preconditions;
import com.google.inject.Module;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Optional;
import java.util.Set;

public class Configuration {

  public static final String PORT_PROPERTY_NAME = "PORT";
  public static final String CONTEXT_PATH_PROPERTY_NAME = "APP_ROOT";

  private final Integer port;
  private final Package basePackage;
  private Optional<String> contextPathMaybe = Optional.empty();
  private final Set<Module> modules = new HashSet<>();

  private Configuration(Integer port, Package basePackage, String contextPathMaybe, Set<Module> modules) {
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
    private Integer port;
    private String appRoot;
    private final Set<Module> modules = new HashSet<>();

    public Builder withBasePackage(Package basePackage) {
      this.basePackage = basePackage;
      return this;
    }

    public Builder withPort(int port) {
      this.port = port;
      return this;
    }

    public Builder withAppRoot(String appRoot) {
      this.appRoot = appRoot;
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

      int actualPort;
      if (port != null) {
        actualPort = port;
      } else {
        actualPort = parsePropertyAsInteger(PORT_PROPERTY_NAME);
      }

      String actualAppRoot;
      if (appRoot != null) {
        actualAppRoot = appRoot;
      } else {
        actualAppRoot = normalizeAppRoot(findProperty(CONTEXT_PATH_PROPERTY_NAME, false));
      }

      return new Configuration(actualPort, basePackage, actualAppRoot, modules);
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

  private static String findProperty(String propertyName, boolean throwIfNotFound) {
    String property = System.getProperty(propertyName);
    if (property == null) {
      property = System.getenv(propertyName);
    }
    if (property == null && throwIfNotFound) {
      throw new RuntimeException("Couldn't find property: " + propertyName);
    }
    return property;
  }

  private static Integer parsePropertyAsInteger(String propertyName) {
    try {
      return Integer.parseInt(findProperty(propertyName, true));
    } catch (NumberFormatException n) {
      throw new RuntimeException("Failed to parse '" + propertyName + "' as an integer");
    }
  }
}
