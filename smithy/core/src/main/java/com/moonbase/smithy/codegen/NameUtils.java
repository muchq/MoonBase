package com.moonbase.smithy.codegen;

import java.util.Set;
import java.util.regex.Pattern;

/**
 * Utilities for name conversions across different languages.
 */
public final class NameUtils {
    private static final Pattern CAMEL_CASE_BOUNDARY = Pattern.compile("(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])");

    private NameUtils() {}

    /**
     * Converts to PascalCase (e.g., "my_name" -> "MyName").
     */
    public static String toPascalCase(String name) {
        if (name == null || name.isEmpty()) {
            return name;
        }
        StringBuilder result = new StringBuilder();
        boolean capitalizeNext = true;
        for (char c : name.toCharArray()) {
            if (c == '_' || c == '-') {
                capitalizeNext = true;
            } else if (capitalizeNext) {
                result.append(Character.toUpperCase(c));
                capitalizeNext = false;
            } else {
                result.append(c);
            }
        }
        return result.toString();
    }

    /**
     * Converts to camelCase (e.g., "my_name" -> "myName").
     */
    public static String toCamelCase(String name) {
        String pascal = toPascalCase(name);
        if (pascal == null || pascal.isEmpty()) {
            return pascal;
        }
        return Character.toLowerCase(pascal.charAt(0)) + pascal.substring(1);
    }

    /**
     * Converts to snake_case (e.g., "MyName" -> "my_name").
     */
    public static String toSnakeCase(String name) {
        if (name == null || name.isEmpty()) {
            return name;
        }
        return CAMEL_CASE_BOUNDARY.matcher(name)
            .replaceAll("_")
            .toLowerCase();
    }

    /**
     * Converts to SCREAMING_SNAKE_CASE (e.g., "MyName" -> "MY_NAME").
     */
    public static String toScreamingSnakeCase(String name) {
        return toSnakeCase(name).toUpperCase();
    }

    /**
     * Converts to kebab-case (e.g., "MyName" -> "my-name").
     */
    public static String toKebabCase(String name) {
        return toSnakeCase(name).replace('_', '-');
    }

    /**
     * Escapes a name if it's a reserved keyword in Java.
     */
    public static String escapeJavaKeyword(String name) {
        return JAVA_KEYWORDS.contains(name) ? name + "_" : name;
    }

    /**
     * Escapes a name if it's a reserved keyword in Go.
     */
    public static String escapeGoKeyword(String name) {
        return GO_KEYWORDS.contains(name) ? name + "_" : name;
    }

    /**
     * Escapes a name if it's a reserved keyword in Rust.
     */
    public static String escapeRustKeyword(String name) {
        return RUST_KEYWORDS.contains(name) ? "r#" + name : name;
    }

    /**
     * Escapes a name if it's a reserved keyword in C++.
     */
    public static String escapeCppKeyword(String name) {
        return CPP_KEYWORDS.contains(name) ? name + "_" : name;
    }

    /**
     * Extracts the simple name from a shape ID (e.g., "com.example#MyShape" -> "MyShape").
     */
    public static String getSimpleName(String shapeId) {
        if (shapeId == null) {
            return null;
        }
        int idx = shapeId.indexOf('#');
        return idx >= 0 ? shapeId.substring(idx + 1) : shapeId;
    }

    /**
     * Extracts the namespace from a shape ID (e.g., "com.example#MyShape" -> "com.example").
     */
    public static String getNamespace(String shapeId) {
        if (shapeId == null) {
            return null;
        }
        int idx = shapeId.indexOf('#');
        return idx >= 0 ? shapeId.substring(0, idx) : "";
    }

    private static final Set<String> JAVA_KEYWORDS = Set.of(
        "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
        "class", "const", "continue", "default", "do", "double", "else", "enum",
        "extends", "final", "finally", "float", "for", "goto", "if", "implements",
        "import", "instanceof", "int", "interface", "long", "native", "new", "package",
        "private", "protected", "public", "return", "short", "static", "strictfp",
        "super", "switch", "synchronized", "this", "throw", "throws", "transient",
        "try", "void", "volatile", "while", "true", "false", "null", "var", "record",
        "sealed", "permits", "yield"
    );

    private static final Set<String> GO_KEYWORDS = Set.of(
        "break", "case", "chan", "const", "continue", "default", "defer", "else",
        "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
        "map", "package", "range", "return", "select", "struct", "switch", "type",
        "var", "true", "false", "nil", "iota", "append", "cap", "close", "complex",
        "copy", "delete", "imag", "len", "make", "new", "panic", "print", "println",
        "real", "recover", "error", "string", "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64", "float32", "float64", "bool", "byte"
    );

    private static final Set<String> RUST_KEYWORDS = Set.of(
        "as", "async", "await", "break", "const", "continue", "crate", "dyn", "else",
        "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop",
        "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self", "static",
        "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while",
        "abstract", "become", "box", "do", "final", "macro", "override", "priv", "try",
        "typeof", "unsized", "virtual", "yield"
    );

    private static final Set<String> CPP_KEYWORDS = Set.of(
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
        "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit",
        "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline",
        "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
        "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
        "register", "reinterpret_cast", "requires", "return", "short", "signed",
        "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
        "template", "this", "thread_local", "throw", "true", "try", "typedef",
        "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
        "volatile", "wchar_t", "while", "xor", "xor_eq"
    );
}
