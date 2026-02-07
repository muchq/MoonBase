package com.moonbase.smithy.java;

import com.moonbase.smithy.codegen.CodeGenerator;
import com.moonbase.smithy.codegen.CodeWriter;
import com.moonbase.smithy.codegen.NameUtils;
import com.moonbase.smithy.model.*;
import com.moonbase.smithy.model.Shape.ShapeType;

import java.io.IOException;
import java.nio.file.Path;
import java.util.Optional;

/**
 * Generates Java server code from Smithy models.
 */
public class JavaServerGenerator implements CodeGenerator {

    @Override
    public String getLanguage() {
        return "java";
    }

    @Override
    public void generate(SmithyModel model, Path outputDir, GeneratorOptions options) throws IOException {
        String packageName = options.getPackageName() != null
            ? options.getPackageName()
            : model.getNamespace().replace("-", ".");

        Path packageDir = outputDir.resolve(packageName.replace(".", "/"));

        // Generate model classes (structures)
        for (Shape shape : model.getShapes().values()) {
            if (shape.getType() == ShapeType.STRUCTURE) {
                generateStructure(shape, model, packageName, packageDir);
            } else if (shape.getType() == ShapeType.ENUM) {
                generateEnum(shape, packageName, packageDir);
            } else if (shape.getType() == ShapeType.UNION) {
                generateUnion(shape, model, packageName, packageDir);
            }
        }

        // Generate service interfaces and handlers
        for (Service service : model.getServices().values()) {
            generateServiceInterface(service, model, packageName, packageDir);
            generateServiceHandler(service, model, packageName, packageDir);

            if (service.isWebSocket()) {
                generateWebSocketHandler(service, model, packageName, packageDir);
            } else {
                generateRouter(service, model, packageName, packageDir);
            }
        }

        // Generate error classes
        generateErrorClasses(model, packageName, packageDir);
    }

    private void generateStructure(Shape shape, SmithyModel model, String packageName, Path packageDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String className = NameUtils.toPascalCase(shape.getName());

        writer.writeLine("package %s;", packageName);
        writer.newLine();
        writer.writeLine("import java.util.Objects;");
        writer.writeLine("import java.util.Optional;");
        writer.newLine();

        // Write documentation
        shape.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
            writer.writeJavaDoc(doc.getStringValue()));

        writer.openBlock("public final class %s", className);

        // Fields
        for (Member member : shape.getMembers()) {
            String fieldType = mapToJavaType(member.getTarget(), model);
            String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
            if (member.isOptional()) {
                fieldType = "Optional<" + boxedType(fieldType) + ">";
            }
            writer.writeLine("private final %s %s;", fieldType, fieldName);
        }
        writer.newLine();

        // Constructor
        writer.write("private %s(", className);
        boolean first = true;
        for (Member member : shape.getMembers()) {
            if (!first) writer.write(", ");
            String fieldType = mapToJavaType(member.getTarget(), model);
            String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
            if (member.isOptional()) {
                fieldType = "Optional<" + boxedType(fieldType) + ">";
            }
            writer.write("%s %s", fieldType, fieldName);
            first = false;
        }
        writer.writeLine(")");
        writer.openBlock("");
        for (Member member : shape.getMembers()) {
            String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
            writer.writeLine("this.%s = %s;", fieldName, fieldName);
        }
        writer.closeBlock();
        writer.newLine();

        // Getters
        for (Member member : shape.getMembers()) {
            String fieldType = mapToJavaType(member.getTarget(), model);
            String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
            String getterName = "get" + NameUtils.toPascalCase(member.getName());
            if (member.isOptional()) {
                fieldType = "Optional<" + boxedType(fieldType) + ">";
            }
            member.getDocumentation().ifPresent(doc -> writer.writeJavaDoc(doc));
            writer.openBlock("public %s %s()", fieldType, getterName);
            writer.writeLine("return %s;", fieldName);
            writer.closeBlock();
            writer.newLine();
        }

        // Builder
        writer.openBlock("public static Builder builder()");
        writer.writeLine("return new Builder();");
        writer.closeBlock();
        writer.newLine();

        // Builder class
        writer.openBlock("public static final class Builder");
        for (Member member : shape.getMembers()) {
            String fieldType = mapToJavaType(member.getTarget(), model);
            String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
            writer.writeLine("private %s %s;", fieldType, fieldName);
        }
        writer.newLine();

        // Builder setters
        for (Member member : shape.getMembers()) {
            String fieldType = mapToJavaType(member.getTarget(), model);
            String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
            String setterName = NameUtils.toCamelCase(member.getName());
            writer.openBlock("public Builder %s(%s %s)", setterName, fieldType, fieldName);
            writer.writeLine("this.%s = %s;", fieldName, fieldName);
            writer.writeLine("return this;");
            writer.closeBlock();
            writer.newLine();
        }

        // Build method
        writer.openBlock("public %s build()", className);
        writer.write("return new %s(", className);
        first = true;
        for (Member member : shape.getMembers()) {
            if (!first) writer.write(", ");
            String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
            if (member.isOptional()) {
                writer.write("Optional.ofNullable(%s)", fieldName);
            } else {
                writer.write("Objects.requireNonNull(%s, \"%s is required\")", fieldName, fieldName);
            }
            first = false;
        }
        writer.writeLine(");");
        writer.closeBlock();
        writer.closeBlock(); // Builder

        writer.closeBlock(); // Class

        writer.writeToFile(packageDir.resolve(className + ".java"));
    }

    private void generateEnum(Shape shape, String packageName, Path packageDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String enumName = NameUtils.toPascalCase(shape.getName());

        writer.writeLine("package %s;", packageName);
        writer.newLine();

        shape.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
            writer.writeJavaDoc(doc.getStringValue()));

        writer.openBlock("public enum %s", enumName);

        boolean first = true;
        for (Member member : shape.getMembers()) {
            if (!first) writer.writeLine(",");
            first = false;
            String valueName = NameUtils.toScreamingSnakeCase(member.getName());
            writer.write("%s(\"%s\")", valueName, member.getName());
        }
        writer.writeLine(";");
        writer.newLine();

        writer.writeLine("private final String value;");
        writer.newLine();

        writer.openBlock("%s(String value)", enumName);
        writer.writeLine("this.value = value;");
        writer.closeBlock();
        writer.newLine();

        writer.openBlock("public String getValue()");
        writer.writeLine("return value;");
        writer.closeBlock();
        writer.newLine();

        writer.openBlock("public static %s fromValue(String value)", enumName);
        writer.openBlock("for (%s e : values())", enumName);
        writer.openBlock("if (e.value.equals(value))");
        writer.writeLine("return e;");
        writer.closeBlock();
        writer.closeBlock();
        writer.writeLine("throw new IllegalArgumentException(\"Unknown %s: \" + value);", enumName);
        writer.closeBlock();

        writer.closeBlock();

        writer.writeToFile(packageDir.resolve(enumName + ".java"));
    }

    private void generateUnion(Shape shape, SmithyModel model, String packageName, Path packageDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String className = NameUtils.toPascalCase(shape.getName());

        writer.writeLine("package %s;", packageName);
        writer.newLine();
        writer.writeLine("import java.util.Objects;");
        writer.newLine();

        shape.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
            writer.writeJavaDoc(doc.getStringValue()));

        writer.openBlock("public abstract sealed class %s", className);

        // Generate variant classes
        for (Member member : shape.getMembers()) {
            String variantName = NameUtils.toPascalCase(member.getName());
            String valueType = mapToJavaType(member.getTarget(), model);

            writer.newLine();
            writer.openBlock("public static final class %s extends %s", variantName, className);
            writer.writeLine("private final %s value;", valueType);
            writer.newLine();
            writer.openBlock("public %s(%s value)", variantName, valueType);
            writer.writeLine("this.value = Objects.requireNonNull(value);");
            writer.closeBlock();
            writer.newLine();
            writer.openBlock("public %s getValue()", valueType);
            writer.writeLine("return value;");
            writer.closeBlock();
            writer.closeBlock();
        }

        // Factory methods
        writer.newLine();
        for (Member member : shape.getMembers()) {
            String variantName = NameUtils.toPascalCase(member.getName());
            String methodName = NameUtils.toCamelCase(member.getName());
            String valueType = mapToJavaType(member.getTarget(), model);

            writer.openBlock("public static %s %s(%s value)", className, methodName, valueType);
            writer.writeLine("return new %s(value);", variantName);
            writer.closeBlock();
            writer.newLine();
        }

        writer.closeBlock();

        writer.writeToFile(packageDir.resolve(className + ".java"));
    }

    private void generateServiceInterface(Service service, SmithyModel model, String packageName, Path packageDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String interfaceName = NameUtils.toPascalCase(service.getName());

        writer.writeLine("package %s;", packageName);
        writer.newLine();
        writer.writeLine("import java.util.concurrent.CompletableFuture;");
        writer.newLine();

        service.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
            writer.writeJavaDoc(doc.getStringValue()));

        writer.openBlock("public interface %s", interfaceName);

        for (Operation op : service.getOperations()) {
            String methodName = NameUtils.toCamelCase(op.getName());
            String inputType = op.getInput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getInput()))
                : "Void";
            String outputType = op.getOutput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getOutput()))
                : "Void";

            writer.newLine();
            op.getDocumentation().ifPresent(doc -> writer.writeJavaDoc(doc));
            writer.writeLine("CompletableFuture<%s> %s(%s input);", outputType, methodName, inputType);
        }

        writer.closeBlock();

        writer.writeToFile(packageDir.resolve(interfaceName + ".java"));
    }

    private void generateServiceHandler(Service service, SmithyModel model, String packageName, Path packageDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String serviceName = NameUtils.toPascalCase(service.getName());
        String handlerName = serviceName + "Handler";

        writer.writeLine("package %s;", packageName);
        writer.newLine();
        writer.writeLine("import java.util.concurrent.CompletableFuture;");
        writer.newLine();

        writer.writeJavaDoc("Abstract handler for " + serviceName + ". Implement this class to provide your service logic.");

        writer.openBlock("public abstract class %s implements %s", handlerName, serviceName);

        for (Operation op : service.getOperations()) {
            String methodName = NameUtils.toCamelCase(op.getName());
            String inputType = op.getInput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getInput()))
                : "Void";
            String outputType = op.getOutput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getOutput()))
                : "Void";

            writer.newLine();
            writer.writeLine("@Override");
            writer.openBlock("public CompletableFuture<%s> %s(%s input)", outputType, methodName, inputType);
            writer.writeLine("return CompletableFuture.supplyAsync(() -> handle%s(input));",
                NameUtils.toPascalCase(op.getName()));
            writer.closeBlock();
            writer.newLine();

            writer.writeJavaDoc("Synchronous handler for " + op.getName() + ". Override this to implement the operation.");
            writer.openBlock("protected abstract %s handle%s(%s input)", outputType,
                NameUtils.toPascalCase(op.getName()), inputType);
            writer.closeBlock();
        }

        writer.closeBlock();

        writer.writeToFile(packageDir.resolve(handlerName + ".java"));
    }

    private void generateRouter(Service service, SmithyModel model, String packageName, Path packageDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String serviceName = NameUtils.toPascalCase(service.getName());
        String routerName = serviceName + "Router";

        writer.writeLine("package %s;", packageName);
        writer.newLine();
        writer.writeLine("import com.moonbase.smithy.runtime.HttpRequest;");
        writer.writeLine("import com.moonbase.smithy.runtime.HttpResponse;");
        writer.writeLine("import com.moonbase.smithy.runtime.Router;");
        writer.writeLine("import com.moonbase.smithy.runtime.JsonCodec;");
        writer.writeLine("import java.util.concurrent.CompletableFuture;");
        writer.writeLine("import java.util.Map;");
        writer.writeLine("import java.util.regex.Pattern;");
        writer.writeLine("import java.util.regex.Matcher;");
        writer.newLine();

        writer.writeJavaDoc("HTTP router for " + serviceName + ". Routes incoming requests to the appropriate operation handler.");

        writer.openBlock("public class %s implements Router", routerName);

        writer.writeLine("private final %s service;", serviceName);
        writer.writeLine("private final JsonCodec codec;");
        writer.newLine();

        // URI patterns for operations
        for (Operation op : service.getOperations()) {
            String uri = op.getHttpUri();
            String patternName = NameUtils.toScreamingSnakeCase(op.getName()) + "_PATTERN";
            String pattern = uri.replaceAll("\\{([^}]+)\\}", "(?<$1>[^/]+)");
            writer.writeLine("private static final Pattern %s = Pattern.compile(\"%s\");", patternName, pattern);
        }
        writer.newLine();

        // Constructor
        writer.openBlock("public %s(%s service, JsonCodec codec)", routerName, serviceName);
        writer.writeLine("this.service = service;");
        writer.writeLine("this.codec = codec;");
        writer.closeBlock();
        writer.newLine();

        // Route method
        writer.writeLine("@Override");
        writer.openBlock("public CompletableFuture<HttpResponse> route(HttpRequest request)");
        writer.writeLine("String method = request.getMethod();");
        writer.writeLine("String path = request.getPath();");
        writer.writeLine("Matcher matcher;");
        writer.newLine();

        for (Operation op : service.getOperations()) {
            String httpMethod = op.getHttpMethod();
            String patternName = NameUtils.toScreamingSnakeCase(op.getName()) + "_PATTERN";
            String methodName = NameUtils.toCamelCase(op.getName());
            String inputType = op.getInput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getInput()))
                : "Void";
            String outputType = op.getOutput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getOutput()))
                : "Void";

            writer.openBlock("if (\"%s\".equals(method) && (matcher = %s.matcher(path)).matches())",
                httpMethod, patternName);
            writer.writeLine("%s input = codec.deserialize(request.getBody(), %s.class);", inputType, inputType);
            writer.openBlock("return service.%s(input).thenApply(output ->", methodName);
            writer.writeLine("return HttpResponse.ok(codec.serialize(output));");
            writer.closeBlock(");");
            writer.closeBlock();
            writer.newLine();
        }

        writer.writeLine("return CompletableFuture.completedFuture(HttpResponse.notFound());");
        writer.closeBlock();

        writer.closeBlock();

        writer.writeToFile(packageDir.resolve(routerName + ".java"));
    }

    private void generateWebSocketHandler(Service service, SmithyModel model, String packageName, Path packageDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String serviceName = NameUtils.toPascalCase(service.getName());
        String handlerName = serviceName + "WebSocketHandler";

        writer.writeLine("package %s;", packageName);
        writer.newLine();
        writer.writeLine("import com.moonbase.smithy.runtime.JsonCodec;");
        writer.writeLine("import com.moonbase.smithy.runtime.WebSocketSession;");
        writer.writeLine("import com.moonbase.smithy.runtime.WebSocketHandler;");
        writer.writeLine("import com.moonbase.smithy.runtime.WebSocketMessage;");
        writer.writeLine("import java.util.Map;");
        writer.writeLine("import java.util.concurrent.ConcurrentHashMap;");
        writer.writeLine("import java.util.function.Consumer;");
        writer.newLine();

        writer.writeJavaDoc("WebSocket handler for " + serviceName + ". Routes incoming messages to the appropriate operation handler.");

        writer.openBlock("public class %s implements WebSocketHandler", handlerName);

        writer.writeLine("private final %s service;", serviceName);
        writer.writeLine("private final JsonCodec codec;");
        writer.writeLine("private final Map<String, WebSocketSession> sessions = new ConcurrentHashMap<>();");
        writer.newLine();

        // Constructor
        writer.openBlock("public %s(%s service, JsonCodec codec)", handlerName, serviceName);
        writer.writeLine("this.service = service;");
        writer.writeLine("this.codec = codec;");
        writer.closeBlock();
        writer.newLine();

        // onConnect
        writer.writeLine("@Override");
        writer.openBlock("public void onConnect(WebSocketSession session)");
        writer.writeLine("sessions.put(session.getId(), session);");
        for (Operation op : service.getWebSocketConnectOperations()) {
            String methodName = NameUtils.toCamelCase(op.getName());
            String inputType = op.getInput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getInput()))
                : "Void";
            if (!inputType.equals("Void")) {
                writer.writeLine("service.%s(%s.builder().build());", methodName, inputType);
            } else {
                writer.writeLine("service.%s(null);", methodName);
            }
        }
        writer.closeBlock();
        writer.newLine();

        // onDisconnect
        writer.writeLine("@Override");
        writer.openBlock("public void onDisconnect(WebSocketSession session)");
        writer.writeLine("sessions.remove(session.getId());");
        for (Operation op : service.getWebSocketDisconnectOperations()) {
            String methodName = NameUtils.toCamelCase(op.getName());
            String inputType = op.getInput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getInput()))
                : "Void";
            if (!inputType.equals("Void")) {
                writer.writeLine("service.%s(%s.builder().build());", methodName, inputType);
            } else {
                writer.writeLine("service.%s(null);", methodName);
            }
        }
        writer.closeBlock();
        writer.newLine();

        // onMessage
        writer.writeLine("@Override");
        writer.openBlock("public void onMessage(WebSocketSession session, WebSocketMessage message)");
        writer.writeLine("String action = message.getAction();");
        writer.writeLine("String payload = message.getPayload();");
        writer.newLine();

        writer.openBlock("switch (action)");
        for (Operation op : service.getWebSocketMessageOperations()) {
            String route = op.getWebSocketRoute();
            String methodName = NameUtils.toCamelCase(op.getName());
            String inputType = op.getInput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getInput()))
                : "Void";
            String outputType = op.getOutput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getOutput()))
                : "Void";

            writer.openBlock("case \"%s\":", route);
            if (!inputType.equals("Void")) {
                writer.writeLine("%s input = codec.deserialize(payload, %s.class);", inputType, inputType);
                writer.openBlock("service.%s(input).thenAccept(output ->", methodName);
            } else {
                writer.openBlock("service.%s(null).thenAccept(output ->", methodName);
            }
            if (!outputType.equals("Void")) {
                writer.writeLine("session.send(new WebSocketMessage(\"%s\", codec.serialize(output)));", route + "Response");
            }
            writer.closeBlock(");");
            writer.writeLine("break;");
            writer.closeBlock();
        }
        writer.openBlock("default:");
        writer.writeLine("session.send(new WebSocketMessage(\"error\", \"{\\\"message\\\":\\\"Unknown action: \" + action + \"\\\"}\"));");
        writer.writeLine("break;");
        writer.closeBlock();
        writer.closeBlock(); // switch
        writer.closeBlock(); // onMessage
        writer.newLine();

        // broadcast method
        writer.writeJavaDoc("Broadcasts a message to all connected sessions.");
        writer.openBlock("public void broadcast(String action, Object data)");
        writer.writeLine("String payload = codec.serialize(data);");
        writer.writeLine("WebSocketMessage message = new WebSocketMessage(action, payload);");
        writer.writeLine("sessions.values().forEach(session -> session.send(message));");
        writer.closeBlock();
        writer.newLine();

        // send to specific session
        writer.writeJavaDoc("Sends a message to a specific session.");
        writer.openBlock("public void sendTo(String sessionId, String action, Object data)");
        writer.writeLine("WebSocketSession session = sessions.get(sessionId);");
        writer.openBlock("if (session != null)");
        writer.writeLine("session.send(new WebSocketMessage(action, codec.serialize(data)));");
        writer.closeBlock();
        writer.closeBlock();
        writer.newLine();

        // getSession
        writer.openBlock("public WebSocketSession getSession(String sessionId)");
        writer.writeLine("return sessions.get(sessionId);");
        writer.closeBlock();
        writer.newLine();

        // getSessionCount
        writer.openBlock("public int getSessionCount()");
        writer.writeLine("return sessions.size();");
        writer.closeBlock();

        writer.closeBlock(); // class

        writer.writeToFile(packageDir.resolve(handlerName + ".java"));
    }

    private void generateErrorClasses(SmithyModel model, String packageName, Path packageDir) throws IOException {
        for (Shape shape : model.getShapes().values()) {
            if (shape.hasTrait(Trait.ERROR)) {
                CodeWriter writer = new CodeWriter();
                String className = NameUtils.toPascalCase(shape.getName());

                writer.writeLine("package %s;", packageName);
                writer.newLine();

                String errorType = shape.getTrait(Trait.ERROR)
                    .map(Trait::getStringValue)
                    .orElse("server");
                int statusCode = shape.getTrait(Trait.HTTP_ERROR)
                    .flatMap(Trait::getNumberValue)
                    .map(Number::intValue)
                    .orElse(errorType.equals("client") ? 400 : 500);

                shape.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
                    writer.writeJavaDoc(doc.getStringValue()));

                writer.openBlock("public class %s extends RuntimeException", className);

                writer.writeLine("private static final int HTTP_STATUS_CODE = %d;", statusCode);
                writer.newLine();

                // Fields from structure members
                for (Member member : shape.getMembers()) {
                    if (!member.getName().equals("message")) {
                        String fieldType = mapToJavaType(member.getTarget(), model);
                        String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
                        writer.writeLine("private final %s %s;", fieldType, fieldName);
                    }
                }
                writer.newLine();

                // Constructor
                writer.write("public %s(String message", className);
                for (Member member : shape.getMembers()) {
                    if (!member.getName().equals("message")) {
                        String fieldType = mapToJavaType(member.getTarget(), model);
                        String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
                        writer.write(", %s %s", fieldType, fieldName);
                    }
                }
                writer.writeLine(")");
                writer.openBlock("");
                writer.writeLine("super(message);");
                for (Member member : shape.getMembers()) {
                    if (!member.getName().equals("message")) {
                        String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
                        writer.writeLine("this.%s = %s;", fieldName, fieldName);
                    }
                }
                writer.closeBlock();
                writer.newLine();

                // Status code getter
                writer.openBlock("public int getHttpStatusCode()");
                writer.writeLine("return HTTP_STATUS_CODE;");
                writer.closeBlock();

                // Getters for additional fields
                for (Member member : shape.getMembers()) {
                    if (!member.getName().equals("message")) {
                        String fieldType = mapToJavaType(member.getTarget(), model);
                        String fieldName = NameUtils.escapeJavaKeyword(NameUtils.toCamelCase(member.getName()));
                        String getterName = "get" + NameUtils.toPascalCase(member.getName());
                        writer.newLine();
                        writer.openBlock("public %s %s()", fieldType, getterName);
                        writer.writeLine("return %s;", fieldName);
                        writer.closeBlock();
                    }
                }

                writer.closeBlock();

                writer.writeToFile(packageDir.resolve(className + ".java"));
            }
        }
    }

    private String mapToJavaType(String smithyType, SmithyModel model) {
        if (smithyType == null) return "Void";

        String simpleName = NameUtils.getSimpleName(smithyType);

        return switch (smithyType) {
            case "smithy.api#Blob" -> "byte[]";
            case "smithy.api#Boolean" -> "boolean";
            case "smithy.api#String" -> "String";
            case "smithy.api#Byte" -> "byte";
            case "smithy.api#Short" -> "short";
            case "smithy.api#Integer" -> "int";
            case "smithy.api#Long" -> "long";
            case "smithy.api#Float" -> "float";
            case "smithy.api#Double" -> "double";
            case "smithy.api#BigInteger" -> "java.math.BigInteger";
            case "smithy.api#BigDecimal" -> "java.math.BigDecimal";
            case "smithy.api#Timestamp" -> "java.time.Instant";
            case "smithy.api#Document" -> "Object";
            default -> {
                Optional<Shape> shape = model.getShape(simpleName);
                if (shape.isPresent()) {
                    Shape s = shape.get();
                    if (s.getType() == ShapeType.LIST) {
                        yield "java.util.List<" + boxedType(mapToJavaType(s.getTargetShape(), model)) + ">";
                    } else if (s.getType() == ShapeType.SET) {
                        yield "java.util.Set<" + boxedType(mapToJavaType(s.getTargetShape(), model)) + ">";
                    } else if (s.getType() == ShapeType.MAP) {
                        yield "java.util.Map<" + boxedType(mapToJavaType(s.getKeyShape(), model)) + ", " +
                            boxedType(mapToJavaType(s.getTargetShape(), model)) + ">";
                    }
                }
                yield NameUtils.toPascalCase(simpleName);
            }
        };
    }

    private String boxedType(String type) {
        return switch (type) {
            case "boolean" -> "Boolean";
            case "byte" -> "Byte";
            case "short" -> "Short";
            case "int" -> "Integer";
            case "long" -> "Long";
            case "float" -> "Float";
            case "double" -> "Double";
            default -> type;
        };
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("Usage: JavaServerGenerator <model.json> <output-dir> [package-name]");
            System.exit(1);
        }

        var parser = new com.moonbase.smithy.parser.SmithyParser();
        var model = parser.parse(java.nio.file.Path.of(args[0]));
        var options = new GeneratorOptions();

        if (args.length > 2) {
            options.setPackageName(args[2]);
        }

        new JavaServerGenerator().generate(model, java.nio.file.Path.of(args[1]), options);
        System.out.println("Generated Java server code in " + args[1]);
    }
}
