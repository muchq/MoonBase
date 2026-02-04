package com.moonbase.smithy.go;

import com.moonbase.smithy.codegen.CodeGenerator.GeneratorOptions;
import com.moonbase.smithy.model.SmithyModel;
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
 * Tests for GoServerGenerator.
 */
class GoServerGeneratorTest {

    private GoServerGenerator generator;
    private SmithyParser parser;

    @BeforeEach
    void setUp() {
        generator = new GoServerGenerator();
        parser = new SmithyParser();
    }

    @Test
    @DisplayName("should return go as language")
    void shouldReturnGoAsLanguage() {
        assertEquals("go", generator.getLanguage());
    }

    @Nested
    @DisplayName("Types Generation")
    class TypesGeneration {

        @Test
        @DisplayName("should generate types.go with structures")
        void shouldGenerateTypesFile(@TempDir Path tempDir) throws IOException {
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
                        "age": {"target": "smithy.api#Integer"},
                        "email": {"target": "smithy.api#String"}
                      },
                      "traits": {
                        "smithy.api#documentation": "A person"
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path typesFile = tempDir.resolve("types.go");
            assertTrue(Files.exists(typesFile));

            String content = Files.readString(typesFile);

            // Verify package
            assertTrue(content.contains("package example"));

            // Verify struct
            assertTrue(content.contains("type Person struct {"));
            assertTrue(content.contains("Name string `json:\"name\"`"));
            assertTrue(content.contains("Age *int32 `json:\"age,omitempty\"`"));
            assertTrue(content.contains("Email *string `json:\"email,omitempty\"`"));
        }

        @Test
        @DisplayName("should generate enum types")
        void shouldGenerateEnumTypes(@TempDir Path tempDir) throws IOException {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Status": {
                      "type": "enum",
                      "members": {
                        "ACTIVE": {"target": "smithy.api#Unit"},
                        "INACTIVE": {"target": "smithy.api#Unit"}
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            String content = Files.readString(tempDir.resolve("types.go"));

            assertTrue(content.contains("type Status string"));
            assertTrue(content.contains("StatusActive Status = \"ACTIVE\""));
            assertTrue(content.contains("StatusInactive Status = \"INACTIVE\""));
            assertTrue(content.contains("func (Status) Values() []Status"));
        }

        @Test
        @DisplayName("should generate union types")
        void shouldGenerateUnionTypes(@TempDir Path tempDir) throws IOException {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Result": {
                      "type": "union",
                      "members": {
                        "success": {"target": "smithy.api#String"},
                        "failure": {"target": "smithy.api#String"}
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            String content = Files.readString(tempDir.resolve("types.go"));

            assertTrue(content.contains("type Result interface {"));
            assertTrue(content.contains("isResult()"));
            assertTrue(content.contains("type ResultSuccess struct {"));
            assertTrue(content.contains("type ResultFailure struct {"));
            assertTrue(content.contains("func (ResultSuccess) isResult() {}"));
        }
    }

    @Nested
    @DisplayName("Service Generation")
    class ServiceGeneration {

        private final String serviceJson = """
            {
              "smithy": "2.0",
              "shapes": {
                "com.example#ItemService": {
                  "type": "service",
                  "version": "1.0",
                  "operations": [{"target": "com.example#GetItem"}]
                },
                "com.example#GetItem": {
                  "type": "operation",
                  "input": {"target": "com.example#GetItemInput"},
                  "output": {"target": "com.example#GetItemOutput"},
                  "traits": {
                    "smithy.api#http": {"method": "GET", "uri": "/items/{id}", "code": 200}
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
                    "name": {"target": "smithy.api#String"}
                  }
                }
              }
            }
            """;

        @Test
        @DisplayName("should generate service interface")
        void shouldGenerateServiceInterface(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path serviceFile = tempDir.resolve("item_service.go");
            assertTrue(Files.exists(serviceFile));

            String content = Files.readString(serviceFile);

            assertTrue(content.contains("type ItemService interface {"));
            assertTrue(content.contains("GetItem(ctx context.Context, input *GetItemInput) (*GetItemOutput, error)"));
        }

        @Test
        @DisplayName("should generate HTTP handler")
        void shouldGenerateHttpHandler(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path handlerFile = tempDir.resolve("item_service_handler.go");
            assertTrue(Files.exists(handlerFile));

            String content = Files.readString(handlerFile);

            assertTrue(content.contains("type ItemServiceHandler struct {"));
            assertTrue(content.contains("func NewItemServiceHandler(service ItemService) *ItemServiceHandler"));
            assertTrue(content.contains("func (h *ItemServiceHandler) ServeHTTP(w http.ResponseWriter, r *http.Request)"));
            assertTrue(content.contains("regexp.MustCompile"));
        }
    }

    @Nested
    @DisplayName("Error Generation")
    class ErrorGeneration {

        @Test
        @DisplayName("should generate error types")
        void shouldGenerateErrorTypes(@TempDir Path tempDir) throws IOException {
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
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path errorsFile = tempDir.resolve("errors.go");
            assertTrue(Files.exists(errorsFile));

            String content = Files.readString(errorsFile);

            assertTrue(content.contains("type APIError interface {"));
            assertTrue(content.contains("type NotFoundError struct {"));
            assertTrue(content.contains("func (e *NotFoundError) Error() string"));
            assertTrue(content.contains("func (e *NotFoundError) HTTPStatusCode() int"));
            assertTrue(content.contains("return 404"));
        }
    }

    @Test
    @DisplayName("should generate go.mod file")
    void shouldGenerateGoMod(@TempDir Path tempDir) throws IOException {
        String json = """
            {
              "smithy": "2.0",
              "shapes": {}
            }
            """;

        SmithyModel model = parser.parseString(json);
        GeneratorOptions options = new GeneratorOptions().setModuleName("myservice");

        generator.generate(model, tempDir, options);

        Path goModFile = tempDir.resolve("go.mod");
        assertTrue(Files.exists(goModFile));

        String content = Files.readString(goModFile);

        assertTrue(content.contains("module myservice"));
        assertTrue(content.contains("go 1.21"));
    }
}
