package com.moonbase.smithy.model;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Represents a parsed Smithy model containing services, operations, and shapes.
 */
public class SmithyModel {
    private String namespace;
    private String version;
    private final Map<String, Shape> shapes = new HashMap<>();
    private final Map<String, Service> services = new HashMap<>();
    private final Map<String, String> metadata = new HashMap<>();

    public String getNamespace() {
        return namespace;
    }

    public void setNamespace(String namespace) {
        this.namespace = namespace;
    }

    public String getVersion() {
        return version;
    }

    public void setVersion(String version) {
        this.version = version;
    }

    public Map<String, Shape> getShapes() {
        return shapes;
    }

    public void addShape(Shape shape) {
        shapes.put(shape.getName(), shape);
    }

    public Optional<Shape> getShape(String name) {
        return Optional.ofNullable(shapes.get(name));
    }

    public Map<String, Service> getServices() {
        return services;
    }

    public void addService(Service service) {
        services.put(service.getName(), service);
    }

    public Optional<Service> getService(String name) {
        return Optional.ofNullable(services.get(name));
    }

    public Map<String, String> getMetadata() {
        return metadata;
    }

    public void addMetadata(String key, String value) {
        metadata.put(key, value);
    }

    /**
     * Resolves a shape reference to its full qualified name.
     */
    public String resolveShapeId(String shapeId) {
        if (shapeId.contains("#")) {
            return shapeId;
        }
        return namespace + "#" + shapeId;
    }

    /**
     * Gets all operations from all services.
     */
    public List<Operation> getAllOperations() {
        List<Operation> operations = new ArrayList<>();
        for (Service service : services.values()) {
            operations.addAll(service.getOperations());
        }
        return operations;
    }
}
