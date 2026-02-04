package com.moonbase.smithy.model;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Represents a Smithy operation.
 */
public class Operation {
    private String name;
    private String input;  // Shape ID of input structure
    private String output; // Shape ID of output structure
    private final List<String> errors = new ArrayList<>();
    private final Map<String, Trait> traits = new HashMap<>();

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getInput() {
        return input;
    }

    public void setInput(String input) {
        this.input = input;
    }

    public String getOutput() {
        return output;
    }

    public void setOutput(String output) {
        this.output = output;
    }

    public List<String> getErrors() {
        return errors;
    }

    public void addError(String error) {
        errors.add(error);
    }

    public Map<String, Trait> getTraits() {
        return traits;
    }

    public void addTrait(Trait trait) {
        traits.put(trait.getName(), trait);
    }

    public boolean hasTrait(String name) {
        return traits.containsKey(name);
    }

    public Optional<Trait> getTrait(String name) {
        return Optional.ofNullable(traits.get(name));
    }

    /**
     * Gets the HTTP method for this operation (GET, POST, PUT, DELETE, PATCH).
     */
    public String getHttpMethod() {
        return getTrait(Trait.HTTP)
            .flatMap(Trait::getMapValue)
            .map(m -> (String) m.get("method"))
            .orElse("POST");
    }

    /**
     * Gets the HTTP URI pattern for this operation.
     */
    public String getHttpUri() {
        return getTrait(Trait.HTTP)
            .flatMap(Trait::getMapValue)
            .map(m -> (String) m.get("uri"))
            .orElse("/" + name);
    }

    /**
     * Gets the expected HTTP status code for successful responses.
     */
    public int getHttpCode() {
        return getTrait(Trait.HTTP)
            .flatMap(Trait::getMapValue)
            .map(m -> m.get("code"))
            .map(c -> c instanceof Number ? ((Number) c).intValue() : 200)
            .orElse(200);
    }

    public boolean isReadonly() {
        return hasTrait(Trait.READONLY);
    }

    public boolean isIdempotent() {
        return hasTrait(Trait.IDEMPOTENT);
    }

    /**
     * Gets the documentation for this operation if present.
     */
    public Optional<String> getDocumentation() {
        return getTrait(Trait.DOCUMENTATION)
            .or(() -> getTrait("documentation"))
            .map(Trait::getStringValue);
    }

    // WebSocket helpers

    /**
     * Checks if this is a WebSocket operation.
     */
    public boolean isWebSocketOperation() {
        return hasTrait(Trait.WS_CONNECT)
            || hasTrait(Trait.WS_DISCONNECT)
            || hasTrait(Trait.WS_MESSAGE)
            || hasTrait(Trait.WS_SUBSCRIBE)
            || hasTrait(Trait.WS_PUBLISH)
            || hasTrait(Trait.WS_ROUTE);
    }

    /**
     * Gets the WebSocket route/action name for message routing.
     * Falls back to operation name if not specified.
     */
    public String getWebSocketRoute() {
        return getTrait(Trait.WS_ROUTE)
            .flatMap(Trait::getWebSocketRoute)
            .or(() -> getTrait(Trait.WS_MESSAGE)
                .flatMap(Trait::getWebSocketRoute))
            .orElse(name);
    }

    /**
     * Checks if this is a WebSocket connect handler.
     */
    public boolean isWebSocketConnect() {
        return hasTrait(Trait.WS_CONNECT);
    }

    /**
     * Checks if this is a WebSocket disconnect handler.
     */
    public boolean isWebSocketDisconnect() {
        return hasTrait(Trait.WS_DISCONNECT);
    }

    /**
     * Checks if this is a WebSocket message handler.
     */
    public boolean isWebSocketMessage() {
        return hasTrait(Trait.WS_MESSAGE) || hasTrait(Trait.WS_ROUTE);
    }

    /**
     * Checks if this is a WebSocket subscribe operation (client subscribes to events).
     */
    public boolean isWebSocketSubscribe() {
        return hasTrait(Trait.WS_SUBSCRIBE);
    }

    /**
     * Checks if this is a WebSocket publish operation (server pushes to clients).
     */
    public boolean isWebSocketPublish() {
        return hasTrait(Trait.WS_PUBLISH);
    }
}
