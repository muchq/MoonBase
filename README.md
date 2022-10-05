# Logging

This project provides the [slf4j](https://www.slf4j.org/) api and [logback](https://logback.qos.ch/) as a logging implementation. It includes basic [Sentry](https://sentry.io/) integration.

To configure Sentry, set `sentry.dsn` as a system property or `SENTRY_DSN` as an environment variable.

## Build
```
bazel //:logging
```

## Support
None.

# LunarCat
[![Build Status](https://travis-ci.org/muchq/LunarCat.svg?branch=master)](https://travis-ci.org/muchq/LunarCat)

An easy to use, poorly tested web framework built on RestEasy, Jetty, Jackson, and Guice.

## Installation

```
<dependency>
  <groupId>com.muchq</groupId>
  <artifactId>LunarCat</artifactId>
  <version>1.0-SNAPSHOT</version>
</dependency>
```

## Use

```java
public class ExampleService {
  public static void main(String[] args) {
    Configuration configuration = Configuration.newBuilder()
        .withBasePackage(ExampleService.class.getPackage())
        .withModules(new ExampleModule())
        .build();
    new Service(configuration).run();
  }
```

# JsonUtils
[![Build Status](https://travis-ci.org/muchq/JsonUtils.svg?branch=master)](https://travis-ci.org/muchq/JsonUtils)
