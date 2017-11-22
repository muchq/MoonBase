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

