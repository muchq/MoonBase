package com.moonbase.smithy.model;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Represents a Smithy shape (structure, union, enum, list, map, etc.).
 */
public class Shape {
    private String name;
    private ShapeType type;
    private final List<Member> members = new ArrayList<>();
    private final Map<String, Trait> traits = new HashMap<>();
    private String targetShape; // For list, set, map value types
    private String keyShape;    // For map key types

    public enum ShapeType {
        BLOB,
        BOOLEAN,
        STRING,
        BYTE,
        SHORT,
        INTEGER,
        LONG,
        FLOAT,
        DOUBLE,
        BIG_INTEGER,
        BIG_DECIMAL,
        TIMESTAMP,
        DOCUMENT,
        LIST,
        SET,
        MAP,
        STRUCTURE,
        UNION,
        ENUM,
        INT_ENUM,
        RESOURCE,
        OPERATION,
        SERVICE
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public ShapeType getType() {
        return type;
    }

    public void setType(ShapeType type) {
        this.type = type;
    }

    public List<Member> getMembers() {
        return members;
    }

    public void addMember(Member member) {
        members.add(member);
    }

    public Optional<Member> getMember(String name) {
        return members.stream()
            .filter(m -> m.getName().equals(name))
            .findFirst();
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

    public String getTargetShape() {
        return targetShape;
    }

    public void setTargetShape(String targetShape) {
        this.targetShape = targetShape;
    }

    public String getKeyShape() {
        return keyShape;
    }

    public void setKeyShape(String keyShape) {
        this.keyShape = keyShape;
    }

    public boolean isSimpleType() {
        return type == ShapeType.BLOB || type == ShapeType.BOOLEAN ||
               type == ShapeType.STRING || type == ShapeType.BYTE ||
               type == ShapeType.SHORT || type == ShapeType.INTEGER ||
               type == ShapeType.LONG || type == ShapeType.FLOAT ||
               type == ShapeType.DOUBLE || type == ShapeType.BIG_INTEGER ||
               type == ShapeType.BIG_DECIMAL || type == ShapeType.TIMESTAMP ||
               type == ShapeType.DOCUMENT;
    }

    public boolean isAggregate() {
        return type == ShapeType.LIST || type == ShapeType.SET ||
               type == ShapeType.MAP || type == ShapeType.STRUCTURE ||
               type == ShapeType.UNION;
    }
}
