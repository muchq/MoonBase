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
                    {"target": "com.example#SendMessage"},
                    {"target": "com.example#JoinRoom"}
                  ],
                  "traits": {
                    "smithy.ws#websocket": {}
                  }
                },
                "com.example#OnConnect": {
                  "type": "operation",
                  "input": {"target": "com.example#ConnectInput"},
                  "traits": {"smithy.ws#onConnect": {}}
                },
                "com.example#OnDisconnect": {
                  "type": "operation",
                  "traits": {"smithy.ws#onDisconnect": {}}
                },
                "com.example#SendMessage": {
                  "type": "operation",
                  "input": {"target": "com.example#SendMessageInput"},
                  "output": {"target": "com.example#SendMessageOutput"},
                  "traits": {"smithy.ws#onMessage": {"route": "sendMessage"}}
                },
                "com.example#JoinRoom": {
                  "type": "operation",
                  "input": {"target": "com.example#JoinRoomInput"},
                  "output": {"target": "com.example#JoinRoomOutput"},
                  "traits": {"smithy.ws#onMessage": {"route": "joinRoom"}}
                },
                "com.example#ConnectInput": {
                  "type": "structure",
                  "members": {
                    "userId": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
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
                },
                "com.example#JoinRoomInput": {
                  "type": "structure",
                  "members": {
                    "roomId": {"target": "smithy.api#String", "traits": {"smithy.api#required": {}}}
                  }
                },
                "com.example#JoinRoomOutput": {
                  "type": "structure",
                  "members": {
                    "members": {"target": "smithy.api#String"}
                  }
                }
              }
            }
            """;

        @Test
        @DisplayName("should generate WebSocket handler instead of HTTP handler")
        void shouldGenerateWebSocketHandler(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("chat");

            generator.generate(model, tempDir, options);

            // WebSocket handler should be generated
            Path wsHandlerFile = tempDir.resolve("chat_service_websocket_handler.go");
            assertTrue(Files.exists(wsHandlerFile), "WebSocket handler should be generated");

            // HTTP handler should NOT be generated
            Path httpHandlerFile = tempDir.resolve("chat_service_handler.go");
            assertFalse(Files.exists(httpHandlerFile), "HTTP handler should not be generated for WebSocket service");
        }

        @Test
        @DisplayName("should generate WebSocket handler with gorilla/websocket")
        void shouldGenerateWebSocketHandlerWithGorilla(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("chat");

            generator.generate(model, tempDir, options);

            Path wsHandlerFile = tempDir.resolve("chat_service_websocket_handler.go");
            String content = Files.readString(wsHandlerFile);

            // Verify imports
            assertTrue(content.contains("\"github.com/gorilla/websocket\""));
            assertTrue(content.contains("\"sync\""));

            // Verify WebSocketMessage struct
            assertTrue(content.contains("type WebSocketMessage struct {"));
            assertTrue(content.contains("Action  string          `json:\"action\"`"));
            assertTrue(content.contains("Payload json.RawMessage `json:\"payload\"`"));

            // Verify WebSocketSession struct
            assertTrue(content.contains("type WebSocketSession struct {"));
            assertTrue(content.contains("ID         string"));
            assertTrue(content.contains("Conn       *websocket.Conn"));
            assertTrue(content.contains("Attributes map[string]interface{}"));
        }

        @Test
        @DisplayName("should generate handler struct and constructor")
        void shouldGenerateHandlerStructAndConstructor(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("chat");

            generator.generate(model, tempDir, options);

            String content = Files.readString(tempDir.resolve("chat_service_websocket_handler.go"));

            assertTrue(content.contains("type ChatServiceWebSocketHandler struct {"));
            assertTrue(content.contains("service  ChatService"));
            assertTrue(content.contains("sessions map[string]*WebSocketSession"));
            assertTrue(content.contains("mu       sync.RWMutex"));
            assertTrue(content.contains("upgrader websocket.Upgrader"));

            assertTrue(content.contains("func NewChatServiceWebSocketHandler(service ChatService) *ChatServiceWebSocketHandler"));
        }

        @Test
        @DisplayName("should generate HandleConnection with lifecycle methods")
        void shouldGenerateHandleConnection(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("chat");

            generator.generate(model, tempDir, options);

            String content = Files.readString(tempDir.resolve("chat_service_websocket_handler.go"));

            // HandleConnection method
            assertTrue(content.contains("func (h *ChatServiceWebSocketHandler) HandleConnection(ctx context.Context, conn *websocket.Conn, sessionID string)"));

            // Session management
            assertTrue(content.contains("h.sessions[sessionID] = session"));
            assertTrue(content.contains("delete(h.sessions, sessionID)"));

            // Write pump
            assertTrue(content.contains("go h.writePump(session)"));
        }

        @Test
        @DisplayName("should generate message routing switch")
        void shouldGenerateMessageRouting(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("chat");

            generator.generate(model, tempDir, options);

            String content = Files.readString(tempDir.resolve("chat_service_websocket_handler.go"));

            // handleMessage method
            assertTrue(content.contains("func (h *ChatServiceWebSocketHandler) handleMessage(ctx context.Context, session *WebSocketSession, rawMessage []byte)"));

            // Switch statement for routing
            assertTrue(content.contains("switch msg.Action {"));
            assertTrue(content.contains("case \"sendMessage\":"));
            assertTrue(content.contains("case \"joinRoom\":"));
            assertTrue(content.contains("default:"));
            assertTrue(content.contains("Unknown action"));
        }

        @Test
        @DisplayName("should generate broadcast and session management methods")
        void shouldGenerateBroadcastMethods(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("chat");

            generator.generate(model, tempDir, options);

            String content = Files.readString(tempDir.resolve("chat_service_websocket_handler.go"));

            // Broadcast
            assertTrue(content.contains("func (h *ChatServiceWebSocketHandler) Broadcast(action string, data interface{})"));
            assertTrue(content.contains("h.mu.RLock()"));

            // GetSession
            assertTrue(content.contains("func (h *ChatServiceWebSocketHandler) GetSession(sessionID string) *WebSocketSession"));

            // SessionCount
            assertTrue(content.contains("func (h *ChatServiceWebSocketHandler) SessionCount() int"));
        }

        @Test
        @DisplayName("should generate Session.Send method")
        void shouldGenerateSessionSendMethod(@TempDir Path tempDir) throws IOException {
            SmithyModel model = parser.parseString(webSocketServiceJson);
            GeneratorOptions options = new GeneratorOptions().setModuleName("chat");

            generator.generate(model, tempDir, options);

            String content = Files.readString(tempDir.resolve("chat_service_websocket_handler.go"));

            assertTrue(content.contains("func (s *WebSocketSession) Send(action string, data interface{}) error"));
            assertTrue(content.contains("json.Marshal(data)"));
            assertTrue(content.contains("s.send <- msgBytes"));
        }
    }
}
