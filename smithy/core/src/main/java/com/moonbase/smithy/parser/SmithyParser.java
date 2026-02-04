package com.moonbase.smithy.parser;

import com.google.gson.Gson;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.moonbase.smithy.model.*;

import java.io.IOException;
import java.io.Reader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

/**
 * Parser for Smithy models in JSON AST format.
 *
 * Smithy models can be converted to JSON AST using:
 * smithy ast model.smithy > model.json
 */
public class SmithyParser {
    private final Gson gson = new Gson();

    /**
     * Parses a Smithy JSON AST file.
     */
    public SmithyModel parse(Path path) throws IOException {
        try (Reader reader = Files.newBufferedReader(path)) {
            return parse(reader);
        }
    }

    /**
     * Parses a Smithy JSON AST from a reader.
     */
    public SmithyModel parse(Reader reader) {
        JsonObject root = gson.fromJson(reader, JsonObject.class);
        return parseModel(root);
    }

    /**
     * Parses a Smithy JSON AST from a string.
     */
    public SmithyModel parseString(String json) {
        JsonObject root = gson.fromJson(json, JsonObject.class);
        return parseModel(root);
    }

    private SmithyModel parseModel(JsonObject root) {
        SmithyModel model = new SmithyModel();

        // Parse smithy version
        if (root.has("smithy")) {
            model.setVersion(root.get("smithy").getAsString());
        }

        // Parse metadata
        if (root.has("metadata")) {
            JsonObject metadata = root.getAsJsonObject("metadata");
            for (String key : metadata.keySet()) {
                model.addMetadata(key, metadata.get(key).toString());
            }
        }

        // Parse shapes
        if (root.has("shapes")) {
            JsonObject shapes = root.getAsJsonObject("shapes");
            for (String shapeId : shapes.keySet()) {
                JsonObject shapeDef = shapes.getAsJsonObject(shapeId);

                // Extract namespace from first shape
                if (model.getNamespace() == null && shapeId.contains("#")) {
                    model.setNamespace(shapeId.substring(0, shapeId.indexOf('#')));
                }

                String shapeName = shapeId.contains("#")
                    ? shapeId.substring(shapeId.indexOf('#') + 1)
                    : shapeId;

                String type = shapeDef.get("type").getAsString();

                if ("service".equals(type)) {
                    model.addService(parseService(shapeName, shapeDef, shapes));
                } else if (!"operation".equals(type)) {
                    model.addShape(parseShape(shapeName, shapeDef));
                }
            }
        }

        return model;
    }

    private Service parseService(String name, JsonObject def, JsonObject allShapes) {
        Service service = new Service();
        service.setName(name);

        if (def.has("version")) {
            service.setVersion(def.get("version").getAsString());
        }

        // Parse traits
        if (def.has("traits")) {
            JsonObject traits = def.getAsJsonObject("traits");
            for (String traitName : traits.keySet()) {
                service.addTrait(parseTrait(traitName, traits.get(traitName)));

                // Detect protocol
                if (traitName.contains("protocols#")) {
                    service.setProtocol(traitName);
                }
            }
        }

        // Parse operations
        if (def.has("operations")) {
            JsonArray ops = def.getAsJsonArray("operations");
            for (JsonElement opRef : ops) {
                String opId = opRef.getAsJsonObject().get("target").getAsString();
                String opName = opId.contains("#")
                    ? opId.substring(opId.indexOf('#') + 1)
                    : opId;

                if (allShapes.has(opId)) {
                    service.addOperation(parseOperation(opName, allShapes.getAsJsonObject(opId)));
                }
            }
        }

        // Parse errors
        if (def.has("errors")) {
            JsonArray errors = def.getAsJsonArray("errors");
            for (JsonElement errRef : errors) {
                service.addError(errRef.getAsJsonObject().get("target").getAsString());
            }
        }

        // Parse resources
        if (def.has("resources")) {
            JsonArray resources = def.getAsJsonArray("resources");
            for (JsonElement resRef : resources) {
                service.addResource(resRef.getAsJsonObject().get("target").getAsString());
            }
        }

        return service;
    }

    private Operation parseOperation(String name, JsonObject def) {
        Operation operation = new Operation();
        operation.setName(name);

        if (def.has("input")) {
            operation.setInput(def.getAsJsonObject("input").get("target").getAsString());
        }

        if (def.has("output")) {
            operation.setOutput(def.getAsJsonObject("output").get("target").getAsString());
        }

        if (def.has("errors")) {
            JsonArray errors = def.getAsJsonArray("errors");
            for (JsonElement errRef : errors) {
                operation.addError(errRef.getAsJsonObject().get("target").getAsString());
            }
        }

        if (def.has("traits")) {
            JsonObject traits = def.getAsJsonObject("traits");
            for (String traitName : traits.keySet()) {
                operation.addTrait(parseTrait(traitName, traits.get(traitName)));
            }
        }

        return operation;
    }

    private Shape parseShape(String name, JsonObject def) {
        Shape shape = new Shape();
        shape.setName(name);
        shape.setType(parseShapeType(def.get("type").getAsString()));

        // Parse members for structures and unions
        if (def.has("members")) {
            JsonObject members = def.getAsJsonObject("members");
            for (String memberName : members.keySet()) {
                shape.addMember(parseMember(memberName, members.getAsJsonObject(memberName)));
            }
        }

        // Parse target for list/set
        if (def.has("member")) {
            JsonObject member = def.getAsJsonObject("member");
            shape.setTargetShape(member.get("target").getAsString());
        }

        // Parse key/value for map
        if (def.has("key")) {
            shape.setKeyShape(def.getAsJsonObject("key").get("target").getAsString());
        }
        if (def.has("value")) {
            shape.setTargetShape(def.getAsJsonObject("value").get("target").getAsString());
        }

        // Parse traits
        if (def.has("traits")) {
            JsonObject traits = def.getAsJsonObject("traits");
            for (String traitName : traits.keySet()) {
                shape.addTrait(parseTrait(traitName, traits.get(traitName)));
            }
        }

        return shape;
    }

    private Member parseMember(String name, JsonObject def) {
        Member member = new Member();
        member.setName(name);
        member.setTarget(def.get("target").getAsString());

        if (def.has("traits")) {
            JsonObject traits = def.getAsJsonObject("traits");
            for (String traitName : traits.keySet()) {
                member.addTrait(parseTrait(traitName, traits.get(traitName)));
            }
        }

        return member;
    }

    private Trait parseTrait(String name, JsonElement value) {
        Trait trait = new Trait();
        trait.setName(name);
        trait.setValue(convertJsonElement(value));
        return trait;
    }

    private Object convertJsonElement(JsonElement element) {
        if (element.isJsonNull()) {
            return null;
        }
        if (element.isJsonPrimitive()) {
            var primitive = element.getAsJsonPrimitive();
            if (primitive.isBoolean()) {
                return primitive.getAsBoolean();
            }
            if (primitive.isNumber()) {
                return primitive.getAsNumber();
            }
            return primitive.getAsString();
        }
        if (element.isJsonArray()) {
            return element.getAsJsonArray().asList().stream()
                .map(this::convertJsonElement)
                .toList();
        }
        if (element.isJsonObject()) {
            var obj = element.getAsJsonObject();
            var map = new java.util.HashMap<String, Object>();
            for (String key : obj.keySet()) {
                map.put(key, convertJsonElement(obj.get(key)));
            }
            return map;
        }
        return element.toString();
    }

    private Shape.ShapeType parseShapeType(String type) {
        return switch (type) {
            case "blob" -> Shape.ShapeType.BLOB;
            case "boolean" -> Shape.ShapeType.BOOLEAN;
            case "string" -> Shape.ShapeType.STRING;
            case "byte" -> Shape.ShapeType.BYTE;
            case "short" -> Shape.ShapeType.SHORT;
            case "integer" -> Shape.ShapeType.INTEGER;
            case "long" -> Shape.ShapeType.LONG;
            case "float" -> Shape.ShapeType.FLOAT;
            case "double" -> Shape.ShapeType.DOUBLE;
            case "bigInteger" -> Shape.ShapeType.BIG_INTEGER;
            case "bigDecimal" -> Shape.ShapeType.BIG_DECIMAL;
            case "timestamp" -> Shape.ShapeType.TIMESTAMP;
            case "document" -> Shape.ShapeType.DOCUMENT;
            case "list" -> Shape.ShapeType.LIST;
            case "set" -> Shape.ShapeType.SET;
            case "map" -> Shape.ShapeType.MAP;
            case "structure" -> Shape.ShapeType.STRUCTURE;
            case "union" -> Shape.ShapeType.UNION;
            case "enum" -> Shape.ShapeType.ENUM;
            case "intEnum" -> Shape.ShapeType.INT_ENUM;
            case "resource" -> Shape.ShapeType.RESOURCE;
            case "operation" -> Shape.ShapeType.OPERATION;
            case "service" -> Shape.ShapeType.SERVICE;
            default -> Shape.ShapeType.STRUCTURE;
        };
    }
}
