package com.moonbase.smithy.runtime;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonSyntaxException;

/**
 * JSON serialization/deserialization codec.
 */
public class JsonCodec {
    private final Gson gson;

    public JsonCodec() {
        this.gson = new GsonBuilder()
            .setDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'")
            .create();
    }

    public JsonCodec(Gson gson) {
        this.gson = gson;
    }

    /**
     * Serializes an object to JSON.
     */
    public String serialize(Object obj) {
        if (obj == null) {
            return "null";
        }
        return gson.toJson(obj);
    }

    /**
     * Deserializes JSON to an object.
     */
    public <T> T deserialize(String json, Class<T> clazz) {
        if (json == null || json.isEmpty()) {
            try {
                return clazz.getDeclaredConstructor().newInstance();
            } catch (Exception e) {
                return null;
            }
        }
        try {
            return gson.fromJson(json, clazz);
        } catch (JsonSyntaxException e) {
            throw new RuntimeException("Failed to deserialize JSON: " + e.getMessage(), e);
        }
    }

    /**
     * Gets the underlying Gson instance.
     */
    public Gson getGson() {
        return gson;
    }
}
