package com.moonbase.smithy.codegen;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for NameUtils utility class.
 */
class NameUtilsTest {

    @Nested
    @DisplayName("toPascalCase")
    class ToPascalCase {

        @ParameterizedTest
        @CsvSource({
            "my_name, MyName",
            "hello_world, HelloWorld",
            "already_pascal, AlreadyPascal",
            "single, Single",
            "a_b_c, ABC",
            "with-dashes, WithDashes",
            "mixed_with-both, MixedWithBoth"
        })
        @DisplayName("should convert various formats to PascalCase")
        void shouldConvertToPascalCase(String input, String expected) {
            assertEquals(expected, NameUtils.toPascalCase(input));
        }

        @Test
        @DisplayName("should handle null input")
        void shouldHandleNull() {
            assertNull(NameUtils.toPascalCase(null));
        }

        @Test
        @DisplayName("should handle empty input")
        void shouldHandleEmpty() {
            assertEquals("", NameUtils.toPascalCase(""));
        }
    }

    @Nested
    @DisplayName("toCamelCase")
    class ToCamelCase {

        @ParameterizedTest
        @CsvSource({
            "my_name, myName",
            "hello_world, helloWorld",
            "Single, single",
            "UPPER_CASE, upperCase"
        })
        @DisplayName("should convert various formats to camelCase")
        void shouldConvertToCamelCase(String input, String expected) {
            assertEquals(expected, NameUtils.toCamelCase(input));
        }
    }

    @Nested
    @DisplayName("toSnakeCase")
    class ToSnakeCase {

        @ParameterizedTest
        @CsvSource({
            "MyName, my_name",
            "HelloWorld, hello_world",
            "XMLParser, xml_parser",
            "getHTTPResponse, get_http_response",
            "simple, simple"
        })
        @DisplayName("should convert various formats to snake_case")
        void shouldConvertToSnakeCase(String input, String expected) {
            assertEquals(expected, NameUtils.toSnakeCase(input));
        }
    }

    @Nested
    @DisplayName("toScreamingSnakeCase")
    class ToScreamingSnakeCase {

        @ParameterizedTest
        @CsvSource({
            "MyName, MY_NAME",
            "helloWorld, HELLO_WORLD",
            "simple, SIMPLE"
        })
        @DisplayName("should convert to SCREAMING_SNAKE_CASE")
        void shouldConvertToScreamingSnakeCase(String input, String expected) {
            assertEquals(expected, NameUtils.toScreamingSnakeCase(input));
        }
    }

    @Nested
    @DisplayName("toKebabCase")
    class ToKebabCase {

        @ParameterizedTest
        @CsvSource({
            "MyName, my-name",
            "HelloWorld, hello-world",
            "simple, simple"
        })
        @DisplayName("should convert to kebab-case")
        void shouldConvertToKebabCase(String input, String expected) {
            assertEquals(expected, NameUtils.toKebabCase(input));
        }
    }

    @Nested
    @DisplayName("escapeJavaKeyword")
    class EscapeJavaKeyword {

        @Test
        @DisplayName("should escape Java keywords")
        void shouldEscapeKeywords() {
            assertEquals("class_", NameUtils.escapeJavaKeyword("class"));
            assertEquals("interface_", NameUtils.escapeJavaKeyword("interface"));
            assertEquals("void_", NameUtils.escapeJavaKeyword("void"));
            assertEquals("return_", NameUtils.escapeJavaKeyword("return"));
        }

        @Test
        @DisplayName("should not escape non-keywords")
        void shouldNotEscapeNonKeywords() {
            assertEquals("myMethod", NameUtils.escapeJavaKeyword("myMethod"));
            assertEquals("someValue", NameUtils.escapeJavaKeyword("someValue"));
        }
    }

    @Nested
    @DisplayName("escapeGoKeyword")
    class EscapeGoKeyword {

        @Test
        @DisplayName("should escape Go keywords")
        void shouldEscapeKeywords() {
            assertEquals("func_", NameUtils.escapeGoKeyword("func"));
            assertEquals("chan_", NameUtils.escapeGoKeyword("chan"));
            assertEquals("map_", NameUtils.escapeGoKeyword("map"));
            assertEquals("range_", NameUtils.escapeGoKeyword("range"));
        }

        @Test
        @DisplayName("should not escape non-keywords")
        void shouldNotEscapeNonKeywords() {
            assertEquals("myFunc", NameUtils.escapeGoKeyword("myFunc"));
        }
    }

    @Nested
    @DisplayName("escapeRustKeyword")
    class EscapeRustKeyword {

        @Test
        @DisplayName("should escape Rust keywords with r# prefix")
        void shouldEscapeKeywords() {
            assertEquals("r#fn", NameUtils.escapeRustKeyword("fn"));
            assertEquals("r#type", NameUtils.escapeRustKeyword("type"));
            assertEquals("r#match", NameUtils.escapeRustKeyword("match"));
            assertEquals("r#impl", NameUtils.escapeRustKeyword("impl"));
        }

        @Test
        @DisplayName("should not escape non-keywords")
        void shouldNotEscapeNonKeywords() {
            assertEquals("my_func", NameUtils.escapeRustKeyword("my_func"));
        }
    }

    @Nested
    @DisplayName("escapeCppKeyword")
    class EscapeCppKeyword {

        @Test
        @DisplayName("should escape C++ keywords")
        void shouldEscapeKeywords() {
            assertEquals("class_", NameUtils.escapeCppKeyword("class"));
            assertEquals("template_", NameUtils.escapeCppKeyword("template"));
            assertEquals("namespace_", NameUtils.escapeCppKeyword("namespace"));
            assertEquals("virtual_", NameUtils.escapeCppKeyword("virtual"));
        }
    }

    @Nested
    @DisplayName("getSimpleName")
    class GetSimpleName {

        @ParameterizedTest
        @CsvSource({
            "com.example#MyShape, MyShape",
            "smithy.api#String, String",
            "SingleName, SingleName"
        })
        @DisplayName("should extract simple name from shape ID")
        void shouldExtractSimpleName(String input, String expected) {
            assertEquals(expected, NameUtils.getSimpleName(input));
        }

        @Test
        @DisplayName("should handle null input")
        void shouldHandleNull() {
            assertNull(NameUtils.getSimpleName(null));
        }
    }

    @Nested
    @DisplayName("getNamespace")
    class GetNamespace {

        @ParameterizedTest
        @CsvSource({
            "com.example#MyShape, com.example",
            "smithy.api#String, smithy.api"
        })
        @DisplayName("should extract namespace from shape ID")
        void shouldExtractNamespace(String input, String expected) {
            assertEquals(expected, NameUtils.getNamespace(input));
        }

        @Test
        @DisplayName("should return empty for shape ID without namespace")
        void shouldReturnEmptyForNoNamespace() {
            assertEquals("", NameUtils.getNamespace("MyShape"));
        }
    }
}
