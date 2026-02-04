package com.moonbase.smithy.parser;

import com.moonbase.smithy.model.*;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Nested;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Comprehensive tests for the SmithyParser.
 */
class SmithyParserTest {

    private SmithyParser parser;

    @BeforeEach
    void setUp() {
        parser = new SmithyParser();
    }

    @Nested
    @DisplayName("Basic Parsing")
    class BasicParsing {

        @Test
        @DisplayName("should parse smithy version")
        void shouldParseSmithyVersion() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {}
                }
                """;
            SmithyModel model = parser.parseString(json);
            assertEquals("2.0", model.getVersion());
        }

        @Test
        @DisplayName("should parse namespace from shape ID")
        void shouldParseNamespace() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#MyStruct": {
                      "type": "structure",
                      "members": {}
                    }
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);
            assertEquals("com.example", model.getNamespace());
        }

        @Test
        @DisplayName("should parse metadata")
        void shouldParseMetadata() {
            String json = """
                {
                  "smithy": "2.0",
                  "metadata": {
                    "key1": "value1",
                    "key2": "value2"
                  },
                  "shapes": {}
                }
                """;
            SmithyModel model = parser.parseString(json);
            assertEquals("\"value1\"", model.getMetadata().get("key1"));
            assertEquals("\"value2\"", model.getMetadata().get("key2"));
        }
    }

    @Nested
    @DisplayName("Structure Parsing")
    class StructureParsing {

        @Test
        @DisplayName("should parse structure with members")
        void shouldParseStructureWithMembers() {
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
                      }
                    }
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);

            assertTrue(model.getShape("Person").isPresent());
            Shape person = model.getShape("Person").get();

            assertEquals(Shape.ShapeType.STRUCTURE, person.getType());
            assertEquals(2, person.getMembers().size());

            assertTrue(person.getMember("name").isPresent());
            Member nameMember = person.getMember("name").get();
            assertEquals("smithy.api#String", nameMember.getTarget());
            assertTrue(nameMember.isRequired());

            assertTrue(person.getMember("age").isPresent());
            Member ageMember = person.getMember("age").get();
            assertEquals("smithy.api#Integer", ageMember.getTarget());
            assertFalse(ageMember.isRequired());
        }

        @Test
        @DisplayName("should parse structure with documentation trait")
        void shouldParseStructureWithDocumentation() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Person": {
                      "type": "structure",
                      "members": {},
                      "traits": {
                        "smithy.api#documentation": "A person entity"
                      }
                    }
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);

            assertTrue(model.getShape("Person").isPresent());
            Shape person = model.getShape("Person").get();

            assertTrue(person.hasTrait("smithy.api#documentation"));
            assertEquals("A person entity", person.getTrait("smithy.api#documentation").get().getStringValue());
        }
    }

    @Nested
    @DisplayName("Service Parsing")
    class ServiceParsing {

        @Test
        @DisplayName("should parse service with operations")
        void shouldParseServiceWithOperations() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#MyService": {
                      "type": "service",
                      "version": "2024-01-01",
                      "operations": [
                        {"target": "com.example#GetItem"},
                        {"target": "com.example#PutItem"}
                      ],
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
                        "smithy.api#readonly": {}
                      }
                    },
                    "com.example#PutItem": {
                      "type": "operation",
                      "input": {"target": "com.example#PutItemInput"},
                      "output": {"target": "com.example#PutItemOutput"},
                      "traits": {
                        "smithy.api#http": {"method": "PUT", "uri": "/items/{id}", "code": 200},
                        "smithy.api#idempotent": {}
                      }
                    },
                    "com.example#GetItemInput": {
                      "type": "structure",
                      "members": {}
                    },
                    "com.example#GetItemOutput": {
                      "type": "structure",
                      "members": {}
                    },
                    "com.example#PutItemInput": {
                      "type": "structure",
                      "members": {}
                    },
                    "com.example#PutItemOutput": {
                      "type": "structure",
                      "members": {}
                    }
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);

            assertTrue(model.getService("MyService").isPresent());
            Service service = model.getService("MyService").get();

            assertEquals("2024-01-01", service.getVersion());
            assertEquals(2, service.getOperations().size());

            assertTrue(service.getOperation("GetItem").isPresent());
            Operation getItem = service.getOperation("GetItem").get();
            assertEquals("com.example#GetItemInput", getItem.getInput());
            assertEquals("com.example#GetItemOutput", getItem.getOutput());
            assertEquals("GET", getItem.getHttpMethod());
            assertEquals("/items/{id}", getItem.getHttpUri());
            assertTrue(getItem.isReadonly());
            assertFalse(getItem.isIdempotent());

            assertTrue(service.getOperation("PutItem").isPresent());
            Operation putItem = service.getOperation("PutItem").get();
            assertEquals("PUT", putItem.getHttpMethod());
            assertTrue(putItem.isIdempotent());
            assertFalse(putItem.isReadonly());
        }

        @Test
        @DisplayName("should parse service with errors")
        void shouldParseServiceWithErrors() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#MyService": {
                      "type": "service",
                      "version": "1.0",
                      "operations": [],
                      "errors": [
                        {"target": "com.example#NotFoundError"},
                        {"target": "com.example#ValidationError"}
                      ]
                    }
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);

            Service service = model.getService("MyService").get();
            assertEquals(2, service.getErrors().size());
            assertTrue(service.getErrors().contains("com.example#NotFoundError"));
            assertTrue(service.getErrors().contains("com.example#ValidationError"));
        }
    }

    @Nested
    @DisplayName("Enum Parsing")
    class EnumParsing {

        @Test
        @DisplayName("should parse enum with members")
        void shouldParseEnumWithMembers() {
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

            assertTrue(model.getShape("Status").isPresent());
            Shape status = model.getShape("Status").get();

            assertEquals(Shape.ShapeType.ENUM, status.getType());
            assertEquals(3, status.getMembers().size());
        }
    }

    @Nested
    @DisplayName("Collection Parsing")
    class CollectionParsing {

        @Test
        @DisplayName("should parse list type")
        void shouldParseListType() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#StringList": {
                      "type": "list",
                      "member": {"target": "smithy.api#String"}
                    }
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);

            assertTrue(model.getShape("StringList").isPresent());
            Shape list = model.getShape("StringList").get();

            assertEquals(Shape.ShapeType.LIST, list.getType());
            assertEquals("smithy.api#String", list.getTargetShape());
        }

        @Test
        @DisplayName("should parse map type")
        void shouldParseMapType() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#StringMap": {
                      "type": "map",
                      "key": {"target": "smithy.api#String"},
                      "value": {"target": "smithy.api#Integer"}
                    }
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);

            assertTrue(model.getShape("StringMap").isPresent());
            Shape map = model.getShape("StringMap").get();

            assertEquals(Shape.ShapeType.MAP, map.getType());
            assertEquals("smithy.api#String", map.getKeyShape());
            assertEquals("smithy.api#Integer", map.getTargetShape());
        }
    }

    @Nested
    @DisplayName("Error Parsing")
    class ErrorParsing {

        @Test
        @DisplayName("should parse error structure with traits")
        void shouldParseErrorStructure() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#NotFoundError": {
                      "type": "structure",
                      "members": {
                        "message": {
                          "target": "smithy.api#String",
                          "traits": {"smithy.api#required": {}}
                        },
                        "resourceId": {
                          "target": "smithy.api#String"
                        }
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

            assertTrue(model.getShape("NotFoundError").isPresent());
            Shape error = model.getShape("NotFoundError").get();

            assertTrue(error.hasTrait(Trait.ERROR));
            assertEquals("client", error.getTrait(Trait.ERROR).get().getStringValue());
            assertTrue(error.hasTrait(Trait.HTTP_ERROR));
            assertEquals(404, error.getTrait(Trait.HTTP_ERROR).get().getNumberValue().get().intValue());
        }
    }

    @Nested
    @DisplayName("Union Parsing")
    class UnionParsing {

        @Test
        @DisplayName("should parse union with variants")
        void shouldParseUnionWithVariants() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#Result": {
                      "type": "union",
                      "members": {
                        "success": {"target": "smithy.api#String"},
                        "failure": {"target": "com.example#Error"}
                      }
                    }
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);

            assertTrue(model.getShape("Result").isPresent());
            Shape union = model.getShape("Result").get();

            assertEquals(Shape.ShapeType.UNION, union.getType());
            assertEquals(2, union.getMembers().size());

            assertTrue(union.getMember("success").isPresent());
            assertEquals("smithy.api#String", union.getMember("success").get().getTarget());

            assertTrue(union.getMember("failure").isPresent());
            assertEquals("com.example#Error", union.getMember("failure").get().getTarget());
        }
    }

    @Nested
    @DisplayName("Trait Parsing")
    class TraitParsing {

        @Test
        @DisplayName("should parse HTTP trait with all properties")
        void shouldParseHttpTrait() {
            String json = """
                {
                  "smithy": "2.0",
                  "shapes": {
                    "com.example#MyService": {
                      "type": "service",
                      "version": "1.0",
                      "operations": [{"target": "com.example#GetItem"}]
                    },
                    "com.example#GetItem": {
                      "type": "operation",
                      "input": {"target": "com.example#GetItemInput"},
                      "output": {"target": "com.example#GetItemOutput"},
                      "traits": {
                        "smithy.api#http": {
                          "method": "GET",
                          "uri": "/items/{itemId}",
                          "code": 200
                        }
                      }
                    },
                    "com.example#GetItemInput": {"type": "structure", "members": {}},
                    "com.example#GetItemOutput": {"type": "structure", "members": {}}
                  }
                }
                """;
            SmithyModel model = parser.parseString(json);

            Operation op = model.getService("MyService").get().getOperation("GetItem").get();
            assertEquals("GET", op.getHttpMethod());
            assertEquals("/items/{itemId}", op.getHttpUri());
            assertEquals(200, op.getHttpCode());
        }
    }
}
