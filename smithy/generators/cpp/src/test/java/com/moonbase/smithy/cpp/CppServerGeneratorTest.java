package com.moonbase.smithy.cpp;

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
 * Tests for CppServerGenerator.
 */
class CppServerGeneratorTest {

    private CppServerGenerator generator;
    private SmithyParser parser;

    @BeforeEach
    void setUp() {
        generator = new CppServerGenerator();
        parser = new SmithyParser();
    }

    @Test
    @DisplayName("should return cpp as language")
    void shouldReturnCppAsLanguage() {
        assertEquals("cpp", generator.getLanguage());
    }

    @Nested
    @DisplayName("Types Generation")
    class TypesGeneration {

        @Test
        @DisplayName("should generate types.h with structures")
        void shouldGenerateTypesHeader(@TempDir Path tempDir) throws IOException {
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

            Path typesHeader = tempDir.resolve("include/example/types.h");
            assertTrue(Files.exists(typesHeader));

            String content = Files.readString(typesHeader);

            assertTrue(content.contains("#pragma once"));
            assertTrue(content.contains("namespace example {"));
            assertTrue(content.contains("struct Person {"));
            assertTrue(content.contains("std::string name;"));
            assertTrue(content.contains("std::optional<int32_t> age;"));
            assertTrue(content.contains("class Builder {"));
            assertTrue(content.contains("Builder& SetName(std::string value);"));
            assertTrue(content.contains("Person Build();"));
        }

        @Test
        @DisplayName("should generate types.cc with implementations")
        void shouldGenerateTypesSource(@TempDir Path tempDir) throws IOException {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Person": {
                      "type": "structure",
                      "members": {
                        "name": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
                      }
                    }
                  }
                }
                """;

            SmithyModel model = parser.parseString(json);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path typesSource = tempDir.resolve("src/types.cc");
            assertTrue(Files.exists(typesSource));

            String content = Files.readString(typesSource);

            assertTrue(content.contains("#include \"example/types.h\""));
            assertTrue(content.contains("Person::Builder& Person::Builder::SetName(std::string value)"));
            assertTrue(content.contains("Person Person::Builder::Build()"));
            assertTrue(content.contains("void to_json(nlohmann::json& j, const Person& v)"));
            assertTrue(content.contains("void from_json(const nlohmann::json& j, Person& v)"));
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

            String headerContent = Files.readString(tempDir.resolve("include/example/types.h"));
            String sourceContent = Files.readString(tempDir.resolve("src/types.cc"));

            assertTrue(headerContent.contains("enum class Status {"));
            assertTrue(headerContent.contains("Active,"));
            assertTrue(headerContent.contains("Inactive,"));
            assertTrue(headerContent.contains("std::string ToString(Status value);"));
            assertTrue(headerContent.contains("Status StatusFromString(const std::string& str);"));

            assertTrue(sourceContent.contains("std::string ToString(Status value)"));
            assertTrue(sourceContent.contains("case Status::Active: return \"ACTIVE\";"));
            assertTrue(sourceContent.contains("Status StatusFromString(const std::string& str)"));
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

            String headerContent = Files.readString(tempDir.resolve("include/example/types.h"));

            assertTrue(headerContent.contains("struct Result {"));
            assertTrue(headerContent.contains("enum class Type {"));
            assertTrue(headerContent.contains("kSuccess,"));
            assertTrue(headerContent.contains("kFailure,"));
            assertTrue(headerContent.contains("std::variant<std::monostate"));
            assertTrue(headerContent.contains("static Result Success(std::string value);"));
            assertTrue(headerContent.contains("const std::string& AsSuccess() const;"));
            assertTrue(headerContent.contains("bool IsSuccess() const;"));
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
        @DisplayName("should generate service header")
        void shouldGenerateServiceHeader(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path serviceHeader = tempDir.resolve("include/example/item_service.h");
            assertTrue(Files.exists(serviceHeader));

            String content = Files.readString(serviceHeader);

            assertTrue(content.contains("#pragma once"));
            assertTrue(content.contains("class ItemService {"));
            assertTrue(content.contains("virtual ~ItemService() = default;"));
            assertTrue(content.contains("virtual std::expected<GetItemOutput, std::unique_ptr<ServiceError>> GetItem(const GetItemInput& input) = 0;"));
        }

        @Test
        @DisplayName("should generate handler header")
        void shouldGenerateHandlerHeader(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path handlerHeader = tempDir.resolve("include/example/item_service_handler.h");
            assertTrue(Files.exists(handlerHeader));

            String content = Files.readString(handlerHeader);

            assertTrue(content.contains("struct HttpRequest {"));
            assertTrue(content.contains("struct HttpResponse {"));
            assertTrue(content.contains("class ItemServiceHandler {"));
            assertTrue(content.contains("explicit ItemServiceHandler(std::shared_ptr<ItemService> service);"));
            assertTrue(content.contains("HttpResponse Handle(const HttpRequest& request);"));
        }

        @Test
        @DisplayName("should generate handler source")
        void shouldGenerateHandlerSource(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(serviceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("example");

            generator.generate(model, tempDir, options);

            Path handlerSource = tempDir.resolve("src/item_service_handler.cc");
            assertTrue(Files.exists(handlerSource));

            String content = Files.readString(handlerSource);

            assertTrue(content.contains("HttpResponse HttpResponse::Ok(std::string body)"));
            assertTrue(content.contains("HttpResponse HttpResponse::Error(int code, std::string message)"));
            assertTrue(content.contains("static const std::regex kGetItemPattern"));
            assertTrue(content.contains("HttpResponse ItemServiceHandler::Handle(const HttpRequest& request)"));
            assertTrue(content.contains("HttpResponse ItemServiceHandler::HandleGetItem(const HttpRequest& request)"));
        }
    }

    @Nested
    @DisplayName("Error Generation")
    class ErrorGeneration {

        @Test
        @DisplayName("should generate error header")
        void shouldGenerateErrorHeader(@TempDir Path tempDir) throws IOException {
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

            Path errorHeader = tempDir.resolve("include/example/error.h");
            assertTrue(Files.exists(errorHeader));

            String content = Files.readString(errorHeader);

            assertTrue(content.contains("class ServiceError : public std::exception {"));
            assertTrue(content.contains("virtual int HttpStatusCode() const = 0;"));
            assertTrue(content.contains("class NotFoundError : public ServiceError {"));
            assertTrue(content.contains("int HttpStatusCode() const override { return 404; }"));
        }
    }

    @Test
    @DisplayName("should generate CMakeLists.txt")
    void shouldGenerateCMakeLists(@TempDir Path tempDir) throws IOException {
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
        GeneratorOptions options = new GeneratorOptions().setModuleName("my_service");

        generator.generate(model, tempDir, options);

        Path cmakeFile = tempDir.resolve("CMakeLists.txt");
        assertTrue(Files.exists(cmakeFile));

        String content = Files.readString(cmakeFile);

        assertTrue(content.contains("cmake_minimum_required(VERSION 3.20)"));
        assertTrue(content.contains("project(my_service)"));
        assertTrue(content.contains("set(CMAKE_CXX_STANDARD 23)"));
        assertTrue(content.contains("find_package(nlohmann_json REQUIRED)"));
        assertTrue(content.contains("add_library(my_service"));
        assertTrue(content.contains("target_link_libraries(my_service PUBLIC nlohmann_json::nlohmann_json)"));
    }

    @Test
    @DisplayName("should generate BUILD.bazel")
    void shouldGenerateBazelBuild(@TempDir Path tempDir) throws IOException {
        String json = """
            {
              "smithy": "2.0",
              "shapes": {}
            }
            """;

        SmithyModel model = parser.parseString(json);
        GeneratorOptions options = new GeneratorOptions().setModuleName("my_service");

        generator.generate(model, tempDir, options);

        Path bazelFile = tempDir.resolve("BUILD.bazel");
        assertTrue(Files.exists(bazelFile));

        String content = Files.readString(bazelFile);

        assertTrue(content.contains("load(\"@rules_cc//cc:defs.bzl\", \"cc_library\")"));
        assertTrue(content.contains("cc_library("));
        assertTrue(content.contains("name = \"my_service\","));
        assertTrue(content.contains("srcs = glob([\"src/*.cc\"]),"));
        assertTrue(content.contains("hdrs = glob([\"include/my_service/*.h\"]),"));
        assertTrue(content.contains("\"@nlohmann_json//:json\","));
    }
}
