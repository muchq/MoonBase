package com.moonbase.smithy.model;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Represents a Smithy service definition.
 */
public class Service {
    private String name;
    private String version;
    private final List<Operation> operations = new ArrayList<>();
    private final List<String> resources = new ArrayList<>();
    private final List<String> errors = new ArrayList<>();
    private final Map<String, Trait> traits = new HashMap<>();
    private String protocol; // e.g., "aws.protocols#restJson1"

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getVersion() {
        return version;
    }

    public void setVersion(String version) {
        this.version = version;
    }

    public List<Operation> getOperations() {
        return operations;
    }

    public void addOperation(Operation operation) {
        operations.add(operation);
    }

    public Optional<Operation> getOperation(String name) {
        return operations.stream()
            .filter(o -> o.getName().equals(name))
            .findFirst();
    }

    public List<String> getResources() {
        return resources;
    }

    public void addResource(String resource) {
        resources.add(resource);
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

    public String getProtocol() {
        return protocol;
    }

    public void setProtocol(String protocol) {
        this.protocol = protocol;
    }

    /**
     * Determines the protocol from traits if not explicitly set.
     */
    public String detectProtocol() {
        if (protocol != null) {
            return protocol;
        }
        // Check for common protocol traits
        if (hasTrait("aws.protocols#restJson1")) {
            return "restJson1";
        }
        if (hasTrait("aws.protocols#restXml")) {
            return "restXml";
        }
        if (hasTrait("aws.protocols#awsJson1_0")) {
            return "awsJson1_0";
        }
        if (hasTrait("aws.protocols#awsJson1_1")) {
            return "awsJson1_1";
        }
        if (hasTrait("aws.protocols#awsQuery")) {
            return "awsQuery";
        }
        if (hasTrait("aws.protocols#ec2Query")) {
            return "ec2Query";
        }
        return "restJson1"; // Default
    }
}
