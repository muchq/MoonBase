package com.moonbase.smithy.model;

import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Represents a Smithy trait applied to a shape or member.
 */
public class Trait {
    private String name;
    private Object value;

    public Trait() {}

    public Trait(String name) {
        this.name = name;
    }

    public Trait(String name, Object value) {
        this.name = name;
        this.value = value;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public Object getValue() {
        return value;
    }

    public void setValue(Object value) {
        this.value = value;
    }

    public boolean hasValue() {
        return value != null;
    }

    public String getStringValue() {
        return value != null ? value.toString() : null;
    }

    public Optional<Boolean> getBooleanValue() {
        if (value instanceof Boolean) {
            return Optional.of((Boolean) value);
        }
        return Optional.empty();
    }

    public Optional<Number> getNumberValue() {
        if (value instanceof Number) {
            return Optional.of((Number) value);
        }
        return Optional.empty();
    }

    @SuppressWarnings("unchecked")
    public Optional<List<Object>> getListValue() {
        if (value instanceof List) {
            return Optional.of((List<Object>) value);
        }
        return Optional.of(Collections.emptyList());
    }

    @SuppressWarnings("unchecked")
    public Optional<Map<String, Object>> getMapValue() {
        if (value instanceof Map) {
            return Optional.of((Map<String, Object>) value);
        }
        return Optional.of(Collections.emptyMap());
    }

    // Common trait helpers
    public static final String REQUIRED = "smithy.api#required";
    public static final String DOCUMENTATION = "smithy.api#documentation";
    public static final String HTTP = "smithy.api#http";
    public static final String HTTP_LABEL = "smithy.api#httpLabel";
    public static final String HTTP_QUERY = "smithy.api#httpQuery";
    public static final String HTTP_HEADER = "smithy.api#httpHeader";
    public static final String HTTP_PAYLOAD = "smithy.api#httpPayload";
    public static final String HTTP_ERROR = "smithy.api#httpError";
    public static final String ERROR = "smithy.api#error";
    public static final String PAGINATED = "smithy.api#paginated";
    public static final String READONLY = "smithy.api#readonly";
    public static final String IDEMPOTENT = "smithy.api#idempotent";
    public static final String SENSITIVE = "smithy.api#sensitive";
    public static final String DEPRECATED = "smithy.api#deprecated";
    public static final String PATTERN = "smithy.api#pattern";
    public static final String LENGTH = "smithy.api#length";
    public static final String RANGE = "smithy.api#range";
    public static final String ENUM = "smithy.api#enum";

    // WebSocket traits (custom extension for bidirectional communication)
    public static final String WEBSOCKET = "smithy.ws#websocket";
    public static final String WS_CONNECT = "smithy.ws#onConnect";
    public static final String WS_DISCONNECT = "smithy.ws#onDisconnect";
    public static final String WS_MESSAGE = "smithy.ws#onMessage";
    public static final String WS_SUBSCRIBE = "smithy.ws#subscribe";
    public static final String WS_PUBLISH = "smithy.ws#publish";
    public static final String WS_ROUTE = "smithy.ws#route";

    /**
     * Checks if this is a WebSocket-related trait.
     */
    public boolean isWebSocketTrait() {
        return name != null && name.startsWith("smithy.ws#");
    }

    /**
     * Gets the WebSocket route/action for message routing.
     */
    public Optional<String> getWebSocketRoute() {
        return getMapValue().map(m -> (String) m.get("route"));
    }
}
