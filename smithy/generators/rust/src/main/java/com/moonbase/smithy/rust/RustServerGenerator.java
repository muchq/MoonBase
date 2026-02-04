package com.moonbase.smithy.rust;

import com.moonbase.smithy.codegen.CodeGenerator;
import com.moonbase.smithy.codegen.CodeWriter;
import com.moonbase.smithy.codegen.NameUtils;
import com.moonbase.smithy.model.*;
import com.moonbase.smithy.model.Shape.ShapeType;

import java.io.IOException;
import java.nio.file.Path;
import java.util.Optional;

/**
 * Generates Rust server code from Smithy models.
 */
public class RustServerGenerator implements CodeGenerator {

    @Override
    public String getLanguage() {
        return "rust";
    }

    @Override
    public void generate(SmithyModel model, Path outputDir, GeneratorOptions options) throws IOException {
        String crateName = options.getModuleName() != null
            ? options.getModuleName()
            : NameUtils.toSnakeCase(model.getNamespace().replace(".", "_"));

        Path srcDir = outputDir.resolve("src");

        // Generate lib.rs
        generateLib(model, crateName, srcDir);

        // Generate types.rs
        generateTypes(model, srcDir);

        // Generate error.rs
        generateErrors(model, srcDir);

        // Generate services
        for (Service service : model.getServices().values()) {
            generateService(service, model, srcDir);
            generateHandler(service, model, srcDir);
        }

        // Generate Cargo.toml
        generateCargoToml(crateName, outputDir);
    }

    private void generateLib(SmithyModel model, String crateName, Path srcDir) throws IOException {
        CodeWriter writer = new CodeWriter();

        writer.writeLine("//! %s - Generated Smithy Server", crateName);
        writer.writeLine("//!");
        writer.writeLine("//! This crate was generated from a Smithy model.");
        writer.newLine();

        writer.writeLine("pub mod error;");
        writer.writeLine("pub mod types;");
        for (Service service : model.getServices().values()) {
            String modName = NameUtils.toSnakeCase(service.getName());
            writer.writeLine("pub mod %s;", modName);
            writer.writeLine("pub mod %s_handler;", modName);
        }
        writer.newLine();

        writer.writeLine("pub use error::*;");
        writer.writeLine("pub use types::*;");
        for (Service service : model.getServices().values()) {
            String modName = NameUtils.toSnakeCase(service.getName());
            String traitName = NameUtils.toPascalCase(service.getName());
            writer.writeLine("pub use %s::%s;", modName, traitName);
            writer.writeLine("pub use %s_handler::%sHandler;", modName, traitName);
        }

        writer.writeToFile(srcDir.resolve("lib.rs"));
    }

    private void generateTypes(SmithyModel model, Path srcDir) throws IOException {
        CodeWriter writer = new CodeWriter();

        writer.writeLine("//! Type definitions generated from Smithy model");
        writer.newLine();
        writer.writeLine("use serde::{Deserialize, Serialize};");
        writer.newLine();

        for (Shape shape : model.getShapes().values()) {
            if (shape.hasTrait(Trait.ERROR)) {
                continue;
            }

            if (shape.getType() == ShapeType.STRUCTURE) {
                generateStruct(writer, shape, model);
            } else if (shape.getType() == ShapeType.ENUM) {
                generateEnum(writer, shape);
            } else if (shape.getType() == ShapeType.UNION) {
                generateUnion(writer, shape, model);
            }
        }

        writer.writeToFile(srcDir.resolve("types.rs"));
    }

    private void generateStruct(CodeWriter writer, Shape shape, SmithyModel model) {
        String structName = NameUtils.toPascalCase(shape.getName());

        shape.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
            writer.writeDocComment(doc.getStringValue(), "///"));

        writer.writeLine("#[derive(Debug, Clone, Serialize, Deserialize)]");
        writer.writeLine("#[serde(rename_all = \"camelCase\")]");
        writer.writeLine("pub struct %s {", structName);
        writer.indent();

        for (Member member : shape.getMembers()) {
            String fieldName = NameUtils.escapeRustKeyword(NameUtils.toSnakeCase(member.getName()));
            String fieldType = mapToRustType(member.getTarget(), model);

            member.getDocumentation().ifPresent(doc ->
                writer.writeDocComment(doc, "///"));

            if (member.isOptional()) {
                writer.writeLine("#[serde(skip_serializing_if = \"Option::is_none\")]");
                writer.writeLine("pub %s: Option<%s>,", fieldName, fieldType);
            } else {
                writer.writeLine("pub %s: %s,", fieldName, fieldType);
            }
        }

        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        // Builder implementation
        writer.writeLine("impl %s {", structName);
        writer.indent();
        writer.writeLine("pub fn builder() -> %sBuilder {", structName);
        writer.indent();
        writer.writeLine("%sBuilder::default()", structName);
        writer.dedent();
        writer.writeLine("}");
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        // Builder struct
        writer.writeLine("#[derive(Debug, Default)]");
        writer.writeLine("pub struct %sBuilder {", structName);
        writer.indent();
        for (Member member : shape.getMembers()) {
            String fieldName = NameUtils.escapeRustKeyword(NameUtils.toSnakeCase(member.getName()));
            String fieldType = mapToRustType(member.getTarget(), model);
            writer.writeLine("%s: Option<%s>,", fieldName, fieldType);
        }
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        writer.writeLine("impl %sBuilder {", structName);
        writer.indent();

        // Builder setters
        for (Member member : shape.getMembers()) {
            String fieldName = NameUtils.escapeRustKeyword(NameUtils.toSnakeCase(member.getName()));
            String fieldType = mapToRustType(member.getTarget(), model);
            writer.writeLine("pub fn %s(mut self, value: %s) -> Self {", fieldName, fieldType);
            writer.indent();
            writer.writeLine("self.%s = Some(value);", fieldName);
            writer.writeLine("self");
            writer.dedent();
            writer.writeLine("}");
            writer.newLine();
        }

        // Build method
        writer.writeLine("pub fn build(self) -> Result<%s, &'static str> {", structName);
        writer.indent();
        writer.writeLine("Ok(%s {", structName);
        writer.indent();
        for (Member member : shape.getMembers()) {
            String fieldName = NameUtils.escapeRustKeyword(NameUtils.toSnakeCase(member.getName()));
            if (member.isOptional()) {
                writer.writeLine("%s: self.%s,", fieldName, fieldName);
            } else {
                writer.writeLine("%s: self.%s.ok_or(\"%s is required\")?,", fieldName, fieldName, fieldName);
            }
        }
        writer.dedent();
        writer.writeLine("})");
        writer.dedent();
        writer.writeLine("}");

        writer.dedent();
        writer.writeLine("}");
        writer.newLine();
    }

    private void generateEnum(CodeWriter writer, Shape shape) {
        String enumName = NameUtils.toPascalCase(shape.getName());

        shape.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
            writer.writeDocComment(doc.getStringValue(), "///"));

        writer.writeLine("#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]");
        writer.writeLine("pub enum %s {", enumName);
        writer.indent();

        for (Member member : shape.getMembers()) {
            String variantName = NameUtils.toPascalCase(member.getName());
            writer.writeLine("#[serde(rename = \"%s\")]", member.getName());
            writer.writeLine("%s,", variantName);
        }

        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        // FromStr implementation
        writer.writeLine("impl std::str::FromStr for %s {", enumName);
        writer.indent();
        writer.writeLine("type Err = String;");
        writer.newLine();
        writer.writeLine("fn from_str(s: &str) -> Result<Self, Self::Err> {");
        writer.indent();
        writer.writeLine("match s {");
        writer.indent();
        for (Member member : shape.getMembers()) {
            String variantName = NameUtils.toPascalCase(member.getName());
            writer.writeLine("\"%s\" => Ok(%s::%s),", member.getName(), enumName, variantName);
        }
        writer.writeLine("_ => Err(format!(\"Unknown %s: {}\", s)),", enumName);
        writer.dedent();
        writer.writeLine("}");
        writer.dedent();
        writer.writeLine("}");
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();
    }

    private void generateUnion(CodeWriter writer, Shape shape, SmithyModel model) {
        String enumName = NameUtils.toPascalCase(shape.getName());

        shape.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
            writer.writeDocComment(doc.getStringValue(), "///"));

        writer.writeLine("#[derive(Debug, Clone, Serialize, Deserialize)]");
        writer.writeLine("#[serde(untagged)]");
        writer.writeLine("pub enum %s {", enumName);
        writer.indent();

        for (Member member : shape.getMembers()) {
            String variantName = NameUtils.toPascalCase(member.getName());
            String valueType = mapToRustType(member.getTarget(), model);
            writer.writeLine("%s(%s),", variantName, valueType);
        }

        writer.dedent();
        writer.writeLine("}");
        writer.newLine();
    }

    private void generateErrors(SmithyModel model, Path srcDir) throws IOException {
        CodeWriter writer = new CodeWriter();

        writer.writeLine("//! Error types generated from Smithy model");
        writer.newLine();
        writer.writeLine("use serde::{Deserialize, Serialize};");
        writer.writeLine("use std::fmt;");
        writer.newLine();

        // Collect error types
        boolean hasErrors = false;
        for (Shape shape : model.getShapes().values()) {
            if (shape.hasTrait(Trait.ERROR)) {
                hasErrors = true;
                break;
            }
        }

        // Service error enum
        writer.writeLine("/// Error type encompassing all service errors");
        writer.writeLine("#[derive(Debug)]");
        writer.writeLine("pub enum ServiceError {");
        writer.indent();
        for (Shape shape : model.getShapes().values()) {
            if (shape.hasTrait(Trait.ERROR)) {
                String errorName = NameUtils.toPascalCase(shape.getName());
                writer.writeLine("%s(%s),", errorName, errorName);
            }
        }
        writer.writeLine("Unhandled(Box<dyn std::error::Error + Send + Sync>),");
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        writer.writeLine("impl fmt::Display for ServiceError {");
        writer.indent();
        writer.writeLine("fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {");
        writer.indent();
        writer.writeLine("match self {");
        writer.indent();
        for (Shape shape : model.getShapes().values()) {
            if (shape.hasTrait(Trait.ERROR)) {
                String errorName = NameUtils.toPascalCase(shape.getName());
                writer.writeLine("ServiceError::%s(e) => write!(f, \"{}\", e),", errorName);
            }
        }
        writer.writeLine("ServiceError::Unhandled(e) => write!(f, \"Unhandled error: {}\", e),");
        writer.dedent();
        writer.writeLine("}");
        writer.dedent();
        writer.writeLine("}");
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        writer.writeLine("impl std::error::Error for ServiceError {}");
        writer.newLine();

        writer.writeLine("impl ServiceError {");
        writer.indent();
        writer.writeLine("pub fn http_status_code(&self) -> u16 {");
        writer.indent();
        writer.writeLine("match self {");
        writer.indent();
        for (Shape shape : model.getShapes().values()) {
            if (shape.hasTrait(Trait.ERROR)) {
                String errorName = NameUtils.toPascalCase(shape.getName());
                String errorType = shape.getTrait(Trait.ERROR)
                    .map(Trait::getStringValue)
                    .orElse("server");
                int statusCode = shape.getTrait(Trait.HTTP_ERROR)
                    .flatMap(Trait::getNumberValue)
                    .map(Number::intValue)
                    .orElse(errorType.equals("client") ? 400 : 500);
                writer.writeLine("ServiceError::%s(_) => %d,", errorName, statusCode);
            }
        }
        writer.writeLine("ServiceError::Unhandled(_) => 500,");
        writer.dedent();
        writer.writeLine("}");
        writer.dedent();
        writer.writeLine("}");
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        // Individual error structs
        for (Shape shape : model.getShapes().values()) {
            if (shape.hasTrait(Trait.ERROR)) {
                String errorName = NameUtils.toPascalCase(shape.getName());

                shape.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
                    writer.writeDocComment(doc.getStringValue(), "///"));

                writer.writeLine("#[derive(Debug, Clone, Serialize, Deserialize)]");
                writer.writeLine("pub struct %s {", errorName);
                writer.indent();
                writer.writeLine("pub message: String,");
                for (Member member : shape.getMembers()) {
                    if (!member.getName().equals("message")) {
                        String fieldName = NameUtils.escapeRustKeyword(NameUtils.toSnakeCase(member.getName()));
                        String fieldType = mapToRustType(member.getTarget(), model);
                        if (member.isOptional()) {
                            writer.writeLine("pub %s: Option<%s>,", fieldName, fieldType);
                        } else {
                            writer.writeLine("pub %s: %s,", fieldName, fieldType);
                        }
                    }
                }
                writer.dedent();
                writer.writeLine("}");
                writer.newLine();

                writer.writeLine("impl fmt::Display for %s {", errorName);
                writer.indent();
                writer.writeLine("fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {");
                writer.indent();
                writer.writeLine("write!(f, \"{}\", self.message)");
                writer.dedent();
                writer.writeLine("}");
                writer.dedent();
                writer.writeLine("}");
                writer.newLine();

                writer.writeLine("impl std::error::Error for %s {}", errorName);
                writer.newLine();

                writer.writeLine("impl From<%s> for ServiceError {", errorName);
                writer.indent();
                writer.writeLine("fn from(err: %s) -> Self {", errorName);
                writer.indent();
                writer.writeLine("ServiceError::%s(err)", errorName);
                writer.dedent();
                writer.writeLine("}");
                writer.dedent();
                writer.writeLine("}");
                writer.newLine();
            }
        }

        writer.writeToFile(srcDir.resolve("error.rs"));
    }

    private void generateService(Service service, SmithyModel model, Path srcDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String traitName = NameUtils.toPascalCase(service.getName());
        String modName = NameUtils.toSnakeCase(service.getName());

        writer.writeLine("//! %s service trait", traitName);
        writer.newLine();
        writer.writeLine("use crate::error::ServiceError;");
        writer.writeLine("use crate::types::*;");
        writer.writeLine("use async_trait::async_trait;");
        writer.newLine();

        service.getTrait(Trait.DOCUMENTATION).ifPresent(doc ->
            writer.writeDocComment(doc.getStringValue(), "///"));

        writer.writeLine("#[async_trait]");
        writer.writeLine("pub trait %s: Send + Sync {", traitName);
        writer.indent();

        for (Operation op : service.getOperations()) {
            String methodName = NameUtils.toSnakeCase(op.getName());
            String inputType = op.getInput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getInput()))
                : "()";
            String outputType = op.getOutput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getOutput()))
                : "()";

            op.getDocumentation().ifPresent(doc ->
                writer.writeDocComment(doc, "///"));
            writer.writeLine("async fn %s(&self, input: %s) -> Result<%s, ServiceError>;",
                methodName, inputType, outputType);
            writer.newLine();
        }

        writer.dedent();
        writer.writeLine("}");

        writer.writeToFile(srcDir.resolve(modName + ".rs"));
    }

    private void generateHandler(Service service, SmithyModel model, Path srcDir) throws IOException {
        CodeWriter writer = new CodeWriter();
        String serviceName = NameUtils.toPascalCase(service.getName());
        String modName = NameUtils.toSnakeCase(service.getName());
        String handlerName = serviceName + "Handler";

        writer.writeLine("//! HTTP handler for %s", serviceName);
        writer.newLine();
        writer.writeLine("use crate::%s::%s;", modName, serviceName);
        writer.writeLine("use crate::types::*;");
        writer.writeLine("use axum::{");
        writer.indent();
        writer.writeLine("extract::State,");
        writer.writeLine("http::StatusCode,");
        writer.writeLine("response::IntoResponse,");
        writer.writeLine("routing::{get, post, put, delete, patch},");
        writer.writeLine("Json, Router,");
        writer.dedent();
        writer.writeLine("};");
        writer.writeLine("use std::sync::Arc;");
        writer.newLine();

        writer.writeDocComment("HTTP handler for " + serviceName, "///");
        writer.writeLine("pub struct %s<T: %s> {", handlerName, serviceName);
        writer.indent();
        writer.writeLine("service: Arc<T>,");
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        writer.writeLine("impl<T: %s + 'static> %s<T> {", serviceName, handlerName);
        writer.indent();
        writer.writeLine("pub fn new(service: T) -> Self {");
        writer.indent();
        writer.writeLine("Self { service: Arc::new(service) }");
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        writer.writeLine("pub fn router(self) -> Router {");
        writer.indent();
        writer.writeLine("Router::new()");
        writer.indent();

        for (Operation op : service.getOperations()) {
            String methodName = NameUtils.toSnakeCase(op.getName());
            String httpMethod = op.getHttpMethod().toLowerCase();
            String uri = op.getHttpUri();
            // Convert Smithy URI patterns to Axum format
            String axumUri = uri.replaceAll("\\{([^}]+)\\}", ":$1");
            writer.writeLine(".route(\"%s\", %s(Self::%s))", axumUri, httpMethod, methodName);
        }

        writer.writeLine(".with_state(self.service)");
        writer.dedent();
        writer.dedent();
        writer.writeLine("}");
        writer.newLine();

        // Individual operation handlers
        for (Operation op : service.getOperations()) {
            String methodName = NameUtils.toSnakeCase(op.getName());
            String inputType = op.getInput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getInput()))
                : "()";
            String outputType = op.getOutput() != null
                ? NameUtils.toPascalCase(NameUtils.getSimpleName(op.getOutput()))
                : "()";
            int successCode = op.getHttpCode();

            writer.writeLine("async fn %s(", methodName);
            writer.indent();
            writer.writeLine("State(service): State<Arc<T>>,");
            if (!inputType.equals("()")) {
                writer.writeLine("Json(input): Json<%s>,", inputType);
            }
            writer.dedent();
            writer.writeLine(") -> impl IntoResponse {");
            writer.indent();

            if (inputType.equals("()")) {
                writer.writeLine("match service.%s(()).await {", methodName);
            } else {
                writer.writeLine("match service.%s(input).await {", methodName);
            }
            writer.indent();
            writer.writeLine("Ok(output) => (StatusCode::from_u16(%d).unwrap(), Json(output)).into_response(),", successCode);
            writer.writeLine("Err(err) => {");
            writer.indent();
            writer.writeLine("let status = StatusCode::from_u16(err.http_status_code()).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR);");
            writer.writeLine("(status, Json(serde_json::json!({ \"error\": err.to_string() }))).into_response()");
            writer.dedent();
            writer.writeLine("}");
            writer.dedent();
            writer.writeLine("}");
            writer.dedent();
            writer.writeLine("}");
            writer.newLine();
        }

        writer.dedent();
        writer.writeLine("}");

        writer.writeToFile(srcDir.resolve(modName + "_handler.rs"));
    }

    private void generateCargoToml(String crateName, Path outputDir) throws IOException {
        CodeWriter writer = new CodeWriter();

        writer.writeLine("[package]");
        writer.writeLine("name = \"%s\"", crateName);
        writer.writeLine("version = \"0.1.0\"");
        writer.writeLine("edition = \"2021\"");
        writer.newLine();
        writer.writeLine("[dependencies]");
        writer.writeLine("async-trait = \"0.1\"");
        writer.writeLine("axum = \"0.7\"");
        writer.writeLine("serde = { version = \"1.0\", features = [\"derive\"] }");
        writer.writeLine("serde_json = \"1.0\"");
        writer.writeLine("tokio = { version = \"1\", features = [\"full\"] }");
        writer.writeLine("thiserror = \"1.0\"");

        writer.writeToFile(outputDir.resolve("Cargo.toml"));
    }

    private String mapToRustType(String smithyType, SmithyModel model) {
        if (smithyType == null) return "()"

        String simpleName = NameUtils.getSimpleName(smithyType);

        return switch (smithyType) {
            case "smithy.api#Blob" -> "Vec<u8>";
            case "smithy.api#Boolean" -> "bool";
            case "smithy.api#String" -> "String";
            case "smithy.api#Byte" -> "i8";
            case "smithy.api#Short" -> "i16";
            case "smithy.api#Integer" -> "i32";
            case "smithy.api#Long" -> "i64";
            case "smithy.api#Float" -> "f32";
            case "smithy.api#Double" -> "f64";
            case "smithy.api#BigInteger" -> "num_bigint::BigInt";
            case "smithy.api#BigDecimal" -> "rust_decimal::Decimal";
            case "smithy.api#Timestamp" -> "chrono::DateTime<chrono::Utc>";
            case "smithy.api#Document" -> "serde_json::Value";
            default -> {
                Optional<Shape> shape = model.getShape(simpleName);
                if (shape.isPresent()) {
                    Shape s = shape.get();
                    if (s.getType() == ShapeType.LIST || s.getType() == ShapeType.SET) {
                        yield "Vec<" + mapToRustType(s.getTargetShape(), model) + ">";
                    } else if (s.getType() == ShapeType.MAP) {
                        yield "std::collections::HashMap<" + mapToRustType(s.getKeyShape(), model) + ", " +
                            mapToRustType(s.getTargetShape(), model) + ">";
                    }
                }
                yield NameUtils.toPascalCase(simpleName);
            }
        };
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("Usage: RustServerGenerator <model.json> <output-dir> [crate-name]");
            System.exit(1);
        }

        var parser = new com.moonbase.smithy.parser.SmithyParser();
        var model = parser.parse(java.nio.file.Path.of(args[0]));
        var options = new GeneratorOptions();

        if (args.length > 2) {
            options.setModuleName(args[2]);
        }

        new RustServerGenerator().generate(model, java.nio.file.Path.of(args[1]), options);
        System.out.println("Generated Rust server code in " + args[1]);
    }
}
