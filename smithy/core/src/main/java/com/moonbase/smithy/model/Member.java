package com.moonbase.smithy.model;

import java.util.HashMap;
import java.util.Map;
import java.util.Optional;

/**
 * Represents a member of a structure or union shape.
 */
public class Member {
    private String name;
    private String target;
    private final Map<String, Trait> traits = new HashMap<>();

    public Member() {}

    public Member(String name, String target) {
        this.name = name;
        this.target = target;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getTarget() {
        return target;
    }

    public void setTarget(String target) {
        this.target = target;
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

    public boolean isRequired() {
        return hasTrait("required") || hasTrait("smithy.api#required");
    }

    public boolean isOptional() {
        return !isRequired();
    }

    /**
     * Gets the documentation for this member if present.
     */
    public Optional<String> getDocumentation() {
        return getTrait("documentation")
            .or(() -> getTrait("smithy.api#documentation"))
            .map(Trait::getStringValue);
    }
}
