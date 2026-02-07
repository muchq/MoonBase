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

    @Nested
    @DisplayName("WebSocket Service Generation")
    class WebSocketServiceGeneration {

        private final String webSocketServiceJson = """
            {
              "smithy": "2.0",
              "shapes": {
                "com.example#ChatService": {
                  "type": "service",
                  "version": "1.0",
                  "operations": [
                    {"target": "com.example#OnConnect"},
                    {"target": "com.example#OnDisconnect"},
                    {"target": "com.example#SendMessage"}
                  ],
                  "traits": {
                    "smithy.ws#websocket": {},
                    "smithy.api#documentation": "A WebSocket chat service"
                  }
                },
                "com.example#OnConnect": {
                  "type": "operation",
                  "input": {"target": "com.example#ConnectInput"},
                  "output": {"target": "com.example#ConnectOutput"},
                  "traits": {
                    "smithy.ws#onConnect": {}
                  }
                },
                "com.example#OnDisconnect": {
                  "type": "operation",
                  "input": {"target": "com.example#DisconnectInput"},
                  "traits": {
                    "smithy.ws#onDisconnect": {}
                  }
                },
                "com.example#SendMessage": {
                  "type": "operation",
                  "input": {"target": "com.example#SendMessageInput"},
                  "output": {"target": "com.example#SendMessageOutput"},
                  "traits": {
                    "smithy.ws#onMessage": {"route": "sendMessage"},
                    "smithy.api#documentation": "Send a message"
                  }
                },
                "com.example#ConnectInput": {
                  "type": "structure",
                  "members": {
                    "userId": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
                  }
                },
                "com.example#ConnectOutput": {
                  "type": "structure",
                  "members": {
                    "sessionId": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
                  }
                },
                "com.example#DisconnectInput": {
                  "type": "structure",
                  "members": {
                    "reason": {"target": "smithy.api#String"}
                  }
                },
                "com.example#SendMessageInput": {
                  "type": "structure",
                  "members": {
                    "roomId": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}},
                    "content": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
                  }
                },
                "com.example#SendMessageOutput": {
                  "type": "structure",
                  "members": {
                    "messageId": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
                  }
                }
              }
            }
            """;

        @Test
        @DisplayName("should detect WebSocket service")
        void shouldDetectWebSocketService() throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);

            Service service = model.getServices().values().iterator().next();
            assertTrue(service.isWebSocket(), "Service should be detected as WebSocket");
        }

        @Test
        @DisplayName("should generate WebSocket handler instead of HTTP router")
        void shouldGenerateWebSocketHandler(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            // WebSocket handler should be generated
            Path wsHandlerFile = tempDir.resolve("com/example/ChatServiceWebSocketHandler.java");
            assertTrue(Files.exists(wsHandlerFile), "WebSocket handler should be generated");

            // HTTP router should NOT be generated for WebSocket services
            Path routerFile = tempDir.resolve("com/example/ChatServiceRouter.java");
            assertFalse(Files.exists(routerFile), "HTTP router should not be generated for WebSocket service");
        }

        @Test
        @DisplayName("should generate WebSocket handler with onConnect/onDisconnect/onMessage")
        void shouldGenerateWebSocketHandlerMethods(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path wsHandlerFile = tempDir.resolve("com/example/ChatServiceWebSocketHandler.java");
            String content = Files.readString(wsHandlerFile);

            // Verify class structure
            assertTrue(content.contains("public class ChatServiceWebSocketHandler implements WebSocketHandler"));

            // Verify lifecycle methods
            assertTrue(content.contains("public void onConnect(WebSocketSession session)"));
            assertTrue(content.contains("public void onDisconnect(WebSocketSession session)"));
            assertTrue(content.contains("public void onMessage(WebSocketSession session, WebSocketMessage message)"));

            // Verify session management
            assertTrue(content.contains("sessions.put(session.getId(), session)"));
            assertTrue(content.contains("sessions.remove(session.getId())"));

            // Verify message routing
            assertTrue(content.contains("switch (action)"));
            assertTrue(content.contains("case \"sendMessage\":"));

            // Verify broadcast/send methods
            assertTrue(content.contains("public void broadcast(String action, Object data)"));
            assertTrue(content.contains("public void sendTo(String sessionId, String action, Object data)"));
        }

        @Test
        @DisplayName("should generate service interface for WebSocket service")
        void shouldGenerateServiceInterfaceForWebSocket(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setPackageName("com.example");

            generator.generate(model, tempDir, options);

            Path serviceFile = tempDir.resolve("com/example/ChatService.java");
            assertTrue(Files.exists(serviceFile));

            String content = Files.readString(serviceFile);

            assertTrue(content.contains("public interface ChatService"));
            assertTrue(content.contains("CompletableFuture<ConnectOutput> onConnect(ConnectInput input)"));
            assertTrue(content.contains("CompletableFuture<Void> onDisconnect(DisconnectInput input)"));
            assertTrue(content.contains("CompletableFuture<SendMessageOutput> sendMessage(SendMessageInput input)"));
        }

        @Test
        @DisplayName("should categorize WebSocket operations correctly")
        void shouldCategorizeWebSocketOperations() throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);

            Service service = model.getServices().values().iterator().next();

            assertEquals(1, service.getWebSocketConnectOperations().size(),
                "Should have 1 connect operation");
            assertEquals(1, service.getWebSocketDisconnectOperations().size(),
                "Should have 1 disconnect operation");
            assertEquals(1, service.getWebSocketMessageOperations().size(),
                "Should have 1 message operation");
        }

        @Test
        @DisplayName("should get WebSocket route from operation")
        void shouldGetWebSocketRoute() throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);

            Service service = model.getServices().values().iterator().next();
            Operation sendMessage = service.getOperation("SendMessage").orElseThrow();

            assertEquals("sendMessage", sendMessage.getWebSocketRoute(),
                "WebSocket route should be extracted from trait");
            assertTrue(sendMessage.isWebSocketMessage(),
                "Operation should be identified as WebSocket message handler");
        }
    }

    @Nested
    @DisplayName("WebSocket Runtime Classes")
    class WebSocketRuntimeClasses {

        @Test
        @DisplayName("WebSocketMessage should parse JSON correctly")
        void webSocketMessageShouldParseJson() {
            String json = "{\"action\":\"sendMessage\",\"payload\":{\"roomId\":\"room1\",\"content\":\"hello\"}}";
            var message = com.moonbase.smithy.runtime.WebSocketMessage.fromJson(json);

            assertEquals("sendMessage", message.getAction());
            assertTrue(message.getPayload().contains("roomId"));
            assertTrue(message.getPayload().contains("room1"));
        }

        @Test
        @DisplayName("WebSocketMessage should serialize to JSON correctly")
        void webSocketMessageShouldSerializeToJson() {
            var message = new com.moonbase.smithy.runtime.WebSocketMessage("test", "{\"key\":\"value\"}");
            String json = message.toJson();

            assertTrue(json.contains("\"action\":\"test\""));
            assertTrue(json.contains("\"payload\":"));
        }

        @Test
        @DisplayName("WebSocketSession test implementation should work")
        void webSocketSessionTestImplShouldWork() {
            var session = com.moonbase.smithy.runtime.WebSocketSession.createTestSession("test-123");

            assertEquals("test-123", session.getId());
            assertTrue(session.isOpen());

            session.setAttribute("user", "alice");
            assertEquals("alice", session.getAttribute("user"));

            session.close();
            assertFalse(session.isOpen());
        }
    }
}
