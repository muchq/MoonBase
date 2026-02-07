package com.moonbase.smithy.rust;

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
 * Tests for RustServerGenerator.
 */
class RustServerGeneratorTest {

    private RustServerGenerator generator;
    private SmithyParser parser;

    @BeforeEach
    void setUp() {
        generator = new RustServerGenerator();
        parser = new SmithyParser();
    }

    @Test
    @DisplayName("should return rust as language")
    void shouldReturnRustAsLanguage() {
        assertEquals("rust", generator.getLanguage());
    }

    @Nested
    @DisplayName("Types Generation")
    class TypesGeneration {

        @Test
        @DisplayName("should generate types.rs with structures")
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
                        "age": {"target": "smithy.api#Integer"}
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path typesFile = tempDir.resolve("src/types.rs");
            assertTrue(Files.exists(typesFile));

            String content = Files.readString(typesFile);

            // Verify derives
            assertTrue(content.contains("#[derive(Debug, Clone, Serialize, Deserialize)]"));

            // Verify struct
            assertTrue(content.contains("pub struct Person {"));
            assertTrue(content.contains("pub name: String,"));
            assertTrue(content.contains("pub age: Option<i32>,"));

            // Verify builder
            assertTrue(content.contains("pub struct PersonBuilder {"));
            assertTrue(content.contains("pub fn builder() -> PersonBuilder"));
            assertTrue(content.contains("pub fn build(self) -> Result<Person, &'static str>"));
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

            String content = Files.readString(tempDir.resolve("src/types.rs"));

            assertTrue(content.contains("#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]"));
            assertTrue(content.contains("pub enum Status {"));
            assertTrue(content.contains("#[serde(rename = \"ACTIVE\")]"));
            assertTrue(content.contains("Active,"));
            assertTrue(content.contains("impl std::str::FromStr for Status"));
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

            String content = Files.readString(tempDir.resolve("src/types.rs"));

            assertTrue(content.contains("#[serde(untagged)]"));
            assertTrue(content.contains("pub enum Result {"));
            assertTrue(content.contains("Success(String),"));
            assertTrue(content.contains("Failure(String),"));
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
        @DisplayName("should generate service trait")
        void shouldGenerateServiceTrait(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path serviceFile = tempDir.resolve("src/item_service.rs");
            assertTrue(Files.exists(serviceFile));

            String content = Files.readString(serviceFile);

            assertTrue(content.contains("#[async_trait]"));
            assertTrue(content.contains("pub trait ItemService: Send + Sync {"));
            assertTrue(content.contains("async fn get_item(&self, input: GetItemInput) -> Result<GetItemOutput, ServiceError>;"));
        }

        @Test
        @DisplayName("should generate Axum handler")
        void shouldGenerateAxumHandler(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path handlerFile = tempDir.resolve("src/item_service_handler.rs");
            assertTrue(Files.exists(handlerFile));

            String content = Files.readString(handlerFile);

            assertTrue(content.contains("use axum::{"));
            assertTrue(content.contains("pub struct ItemServiceHandler<T: ItemService> {"));
            assertTrue(content.contains("pub fn router(self) -> Router"));
            assertTrue(content.contains(".route(\"/items/:id\", get(Self::get_item))"));
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
                        "message": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
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

            Path errorFile = tempDir.resolve("src/error.rs");
            assertTrue(Files.exists(errorFile));

            String content = Files.readString(errorFile);

            assertTrue(content.contains("pub enum ServiceError {"));
            assertTrue(content.contains("NotFoundError(NotFoundError),"));
            assertTrue(content.contains("pub struct NotFoundError {"));
            assertTrue(content.contains("impl fmt::Display for NotFoundError"));
            assertTrue(content.contains("impl std::error::Error for NotFoundError"));
            assertTrue(content.contains("impl From<NotFoundError> for ServiceError"));
            assertTrue(content.contains("ServiceError::NotFoundError(_) => 404,"));
        }
    }

    @Test
    @DisplayName("should generate lib.rs")
    void shouldGenerateLibRs(@TempDir Path tempDir) throws IOException {
        String json = """
            {
              "smithy": "2.0",
              "shapes": {
                "com.example#MyService": {
                  "type": "service",
                  "version": "1.0",
                  "operations": []
                }
              }
            }
            """;

        SmithyModel model = parser.parseString(json);
        GeneratorOptions options = new GeneratorOptions().setModuleName("example");

        generator.generate(model, tempDir, options);

        Path libFile = tempDir.resolve("src/lib.rs");
        assertTrue(Files.exists(libFile));

        String content = Files.readString(libFile);

        assertTrue(content.contains("pub mod error;"));
        assertTrue(content.contains("pub mod types;"));
        assertTrue(content.contains("pub mod my_service;"));
        assertTrue(content.contains("pub use error::*;"));
        assertTrue(content.contains("pub use types::*;"));
    }

    @Test
    @DisplayName("should generate Cargo.toml")
    void shouldGenerateCargoToml(@TempDir Path tempDir) throws IOException {
        String json = """
            {
              "smithy": "2.0",
              "shapes": {}
            }
            """;

        SmithyModel model = parser.parseString(json);
        GeneratorOptions options = new GeneratorOptions().setModuleName("my_service");

        generator.generate(model, tempDir, options);

        Path cargoFile = tempDir.resolve("Cargo.toml");
        assertTrue(Files.exists(cargoFile));

        String content = Files.readString(cargoFile);

        assertTrue(content.contains("[package]"));
        assertTrue(content.contains("name = \"my_service\""));
        assertTrue(content.contains("[dependencies]"));
        assertTrue(content.contains("async-trait"));
        assertTrue(content.contains("axum"));
        assertTrue(content.contains("serde"));
        assertTrue(content.contains("tokio"));
    }
}
