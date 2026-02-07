# JsonUtils

## Features

- Type-safe JSON parsing and serialization courtesy of Jackson

## Example Usage

```java
record Person(String name, int age) {}
var person = JsonUtils.readAs("{\"name\":\"John\",\"age\":30}", Person.class);
```
