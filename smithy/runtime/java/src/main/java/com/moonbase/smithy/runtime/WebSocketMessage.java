package com.moonbase.smithy.runtime;

import java.util.Objects;

/**
 * Represents a WebSocket message with an action/route and payload.
 *
 * <p>Messages are typically JSON-encoded with a structure like:
 * <pre>
 * {
 *   "action": "sendMessage",
 *   "payload": { ... }
 * }
 * </pre>
 */
public final class WebSocketMessage {
    private final String action;
    private final String payload;

    public WebSocketMessage(String action, String payload) {
        this.action = Objects.requireNonNull(action, "action is required");
        this.payload = payload;
    }

    /**
     * Gets the action/route name for this message.
     */
    public String getAction() {
        return action;
    }

    /**
     * Gets the JSON payload of this message.
     */
    public String getPayload() {
        return payload;
    }

    /**
     * Converts this message to a JSON string.
     */
    public String toJson() {
        if (payload == null || payload.isEmpty()) {
            return String.format("{\"action\":\"%s\"}", escapeJson(action));
        }
        return String.format("{\"action\":\"%s\",\"payload\":%s}", escapeJson(action), payload);
    }

    /**
     * Parses a WebSocket message from a JSON string.
     */
    public static WebSocketMessage fromJson(String json) {
        // Simple JSON parsing - in production use a proper JSON library
        String action = extractJsonString(json, "action");
        String payload = extractJsonValue(json, "payload");
        return new WebSocketMessage(action, payload);
    }

    private static String extractJsonString(String json, String key) {
        String pattern = "\"" + key + "\"\\s*:\\s*\"";
        int start = json.indexOf(pattern.replace("\\s*", ""));
        if (start < 0) {
            // Try with whitespace
            for (int i = 0; i < json.length(); i++) {
                int keyStart = json.indexOf("\"" + key + "\"", i);
                if (keyStart < 0) break;
                int colonPos = json.indexOf(":", keyStart + key.length() + 2);
                if (colonPos < 0) break;
                int valueStart = json.indexOf("\"", colonPos + 1);
                if (valueStart < 0) break;
                int valueEnd = json.indexOf("\"", valueStart + 1);
                if (valueEnd > valueStart) {
                    return json.substring(valueStart + 1, valueEnd);
                }
            }
            return null;
        }
        start = json.indexOf("\"", start + key.length() + 4) + 1;
        int end = json.indexOf("\"", start);
        return json.substring(start, end);
    }

    private static String extractJsonValue(String json, String key) {
        String pattern = "\"" + key + "\"";
        int keyStart = json.indexOf(pattern);
        if (keyStart < 0) return null;

        int colonPos = json.indexOf(":", keyStart + pattern.length());
        if (colonPos < 0) return null;

        int valueStart = colonPos + 1;
        while (valueStart < json.length() && Character.isWhitespace(json.charAt(valueStart))) {
            valueStart++;
        }

        if (valueStart >= json.length()) return null;

        char firstChar = json.charAt(valueStart);
        if (firstChar == '{') {
            // Object - find matching brace
            int depth = 1;
            int i = valueStart + 1;
            while (i < json.length() && depth > 0) {
                char c = json.charAt(i);
                if (c == '{') depth++;
                else if (c == '}') depth--;
                i++;
            }
            return json.substring(valueStart, i);
        } else if (firstChar == '[') {
            // Array - find matching bracket
            int depth = 1;
            int i = valueStart + 1;
            while (i < json.length() && depth > 0) {
                char c = json.charAt(i);
                if (c == '[') depth++;
                else if (c == ']') depth--;
                i++;
            }
            return json.substring(valueStart, i);
        } else if (firstChar == '"') {
            // String
            int end = json.indexOf("\"", valueStart + 1);
            return json.substring(valueStart, end + 1);
        } else {
            // Number, boolean, or null
            int end = valueStart;
            while (end < json.length() && !",}]".contains(String.valueOf(json.charAt(end)))) {
                end++;
            }
            return json.substring(valueStart, end).trim();
        }
    }

    private static String escapeJson(String s) {
        return s.replace("\\", "\\\\")
            .replace("\"", "\\\"")
            .replace("\n", "\\n")
            .replace("\r", "\\r")
            .replace("\t", "\\t");
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof WebSocketMessage that)) return false;
        return action.equals(that.action) && Objects.equals(payload, that.payload);
    }

    @Override
    public int hashCode() {
        return Objects.hash(action, payload);
    }

    @Override
    public String toString() {
        return toJson();
    }
}
