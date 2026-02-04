package com.moonbase.smithy.java;

import com.moonbase.smithy.codegen.CodeGenerator.GeneratorOptions;
import com.moonbase.smithy.model.*;
import com.moonbase.smithy.parser.SmithyParser;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for JavaServerGenerator.
 */
class JavaServerGeneratorTest {

    private JavaServerGenerator generator;
    private SmithyParser parser;

    @BeforeEach
    void setUp() {
        generator = new JavaServerGenerator();
        parser = new SmithyParser();
    }

    @Test
    @DisplayName("should return java as language")
    void shouldReturnJavaAsLanguage() {
        assertEquals("java", generator.getLanguage());
    }

    @Nested
    @DisplayName("Structure Generation")
    class StructureGeneration {

        @Test
        @DisplayName("should generate structure with required and optional fields")
        void shouldGenerateStructure(@TempDir Path tempDir) throws IOException {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Person": {
                      "type": "structure",
                      "members": {
                        "name": {
                          "target": "smithy.api#String",
                          "traits": {"smithy.api#required": {}}
                        },
                        "age": {
                          "target": "smithy.api#Integer"
                        }
                      },
                      "traits": {
                        "smithy.api#documentation": "A person"
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path personFile = tempDir.resolve("com/example/Person.java");
            assertTrue(Files.exists(personFile), "Person.java should be generated");

            String content = Files.readString(personFile);

            // Verify package declaration
            assertTrue(content.contains("package com.example;"));

            // Verify class declaration
            assertTrue(content.contains("public final class Person"));

            // Verify fields
            assertTrue(content.contains("private final String name;"));
            assertTrue(content.contains("private final Optional<Integer> age;"));

            // Verify getters
            assertTrue(content.contains("public String getName()"));
            assertTrue(content.contains("public Optional<Integer> getAge()"));

            // Verify builder
            assertTrue(content.contains("public static Builder builder()"));
            assertTrue(content.contains("public static final class Builder"));
            assertTrue(content.contains("public Person build()"));

            // Verify documentation
            assertTrue(content.contains("/**"));
            assertTrue(content.contains("A person"));
        }

        @Test
        @DisplayName("should generate structure with complex types")
        void shouldGenerateStructureWithComplexTypes(@TempDir Path tempDir) throws IOException {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Container": {
                      "type": "structure",
                      "members": {
                        "items": {
                          "target": "com.example#ItemList",
                          "traits": {"smithy.api#required": {}}
                        },
                        "metadata": {
                          "target": "com.example#MetadataMap"
                        }
                      }
                    },
                    "com.example#ItemList": {
                      "type": "list",
                      "member": {"target": "smithy.api#String"}
                    },
                    "com.example#MetadataMap": {
                      "type": "map",
                      "key": {"target": "smithy.api#String"},
                      "value": {"target": "smithy.api#String"}
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path containerFile = tempDir.resolve("com/example/Container.java");
            String content = Files.readString(containerFile);

            // Verify list type
            assertTrue(content.contains("java.util.List<String>"));

            // Verify map type
            assertTrue(content.contains("java.util.Map<String, String>"));
        }
    }

    @Nested
    @DisplayName("Enum Generation")
    class EnumGeneration {

        @Test
        @DisplayName("should generate enum with values")
        void shouldGenerateEnum(@TempDir Path tempDir) throws IOException {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Status": {
                      "type": "enum",
                      "members": {
                        "PENDING": {"target": "smithy.api#Unit"},
                        "ACTIVE": {"target": "smithy.api#Unit"},
                        "INACTIVE": {"target": "smithy.api#Unit"}
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path enumFile = tempDir.resolve("com/example/Status.java");
            assertTrue(Files.exists(enumFile));

            String content = Files.readString(enumFile);

            assertTrue(content.contains("public enum Status"));
            assertTrue(content.contains("PENDING(\"PENDING\")"));
            assertTrue(content.contains("ACTIVE(\"ACTIVE\")"));
            assertTrue(content.contains("INACTIVE(\"INACTIVE\")"));
            assertTrue(content.contains("public String getValue()"));
            assertTrue(content.contains("public static Status fromValue(String value)"));
        }
    }

    @Nested
    @DisplayName("Service Generation")
    class ServiceGeneration {

        private final String serviceJson = """
            {
              "smithy": "2.0",
              "shapes": {
                "com.example#MyService": {
                  "type": "service",
                  "version": "1.0",
                  "operations": [{"target": "com.example#GetItem"}],
                  "traits": {
                    "smithy.api#documentation": "My service"
                  }
                },
                "com.example#GetItem": {
                  "type": "operation",
                  "input": {"target": "com.example#GetItemInput"},
                  "output": {"target": "com.example#GetItemOutput"},
                  "traits": {
                    "smithy.api#http": {"method": "GET", "uri": "/items/{id}", "code": 200},
                    "smithy.api#documentation": "Get an item"
                  }
                },
                "com.example#GetItemInput": {
                  "type": "structure",
                  "members": {
                    "id": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
                  }
                },
                "com.example#GetItemOutput": {
                  "type": "structure",
                  "members": {
                    "item": {"target": "smithy.api#String"}
                  }
                }
              }
            }
            """;

        @Test
        @DisplayName("should generate service interface")
        void shouldGenerateServiceInterface(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path serviceFile = tempDir.resolve("com/example/MyService.java");
            assertTrue(Files.exists(serviceFile));

            String content = Files.readString(serviceFile);

            assertTrue(content.contains("public interface MyService"));
            assertTrue(content.contains("CompletableFuture<GetItemOutput> getItem(GetItemInput input)"));
        }

        @Test
        @DisplayName("should generate service handler")
        void shouldGenerateServiceHandler(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path handlerFile = tempDir.resolve("com/example/MyServiceHandler.java");
            assertTrue(Files.exists(handlerFile));

            String content = Files.readString(handlerFile);

            assertTrue(content.contains("public abstract class MyServiceHandler implements MyService"));
            assertTrue(content.contains("protected abstract GetItemOutput handleGetItem(GetItemInput input)"));
        }

        @Test
        @DisplayName("should generate router")
        void shouldGenerateRouter(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path routerFile = tempDir.resolve("com/example/MyServiceRouter.java");
            assertTrue(Files.exists(routerFile));

            String content = Files.readString(routerFile);

            assertTrue(content.contains("public class MyServiceRouter implements Router"));
            assertTrue(content.contains("Pattern.compile"));
            assertTrue(content.contains("route(HttpRequest request)"));
        }
    }

    @Nested
    @DisplayName("Error Generation")
    class ErrorGeneration {

        @Test
        @DisplayName("should generate error classes")
        void shouldGenerateErrorClasses(@TempDir Path tempDir) throws IOException {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#NotFoundError": {
                      "type": "structure",
                      "members": {
                        "message": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}},
                        "resourceId": {"target": "smithy.api#String"}
                      },
                      "traits": {
                        "smithy.api#error": "client",
                        "smithy.api#httpError": 404
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path errorFile = tempDir.resolve("com/example/NotFoundError.java");
            assertTrue(Files.exists(errorFile));

            String content = Files.readString(errorFile);

            assertTrue(content.contains("public class NotFoundError extends RuntimeException"));
            assertTrue(content.contains("HTTP_STATUS_CODE = 404"));
            assertTrue(content.contains("public int getHttpStatusCode()"));
            assertTrue(content.contains("private final String resourceId"));
        }
    }

    @Nested
    @DisplayName("Union Generation")
    class UnionGeneration {

        @Test
        @DisplayName("should generate sealed union class")
        void shouldGenerateUnion(@TempDir Path tempDir) throws IOException {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Result": {
                      "type": "union",
                      "members": {
                        "success": {"target": "smithy.api#String"},
                        "error": {"target": "smithy.api#String"}
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path unionFile = tempDir.resolve("com/example/Result.java");
            assertTrue(Files.exists(unionFile));

            String content = Files.readString(unionFile);

            assertTrue(content.contains("public abstract sealed class Result"));
            assertTrue(content.contains("public static final class Success extends Result"));
            assertTrue(content.contains("public static final class Error extends Result"));
            assertTrue(content.contains("public static Result success(String value)"));
            assertTrue(content.contains("public static Result error(String value)"));
        }
    }
}
