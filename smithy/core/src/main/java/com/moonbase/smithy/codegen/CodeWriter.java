package com.moonbase.smithy.codegen;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Helper class for generating code with proper indentation.
 */
public class CodeWriter {
    private final StringBuilder buffer = new StringBuilder();
    private final Deque<String> indentStack = new ArrayDeque<>();
    private String currentIndent = "";
    private String indentString = "    ";
    private boolean atLineStart = true;

    public CodeWriter() {}

    public CodeWriter(String indentString) {
        this.indentString = indentString;
    }

    /**
     * Writes a line with the current indentation.
     */
    public CodeWriter writeLine(String line) {
        if (!line.isEmpty()) {
            writeIndentIfNeeded();
            buffer.append(line);
        }
        buffer.append("\n");
        atLineStart = true;
        return this;
    }

    /**
     * Writes text without a newline.
     */
    public CodeWriter write(String text) {
        if (!text.isEmpty()) {
            writeIndentIfNeeded();
            buffer.append(text);
            atLineStart = text.endsWith("\n");
        }
        return this;
    }

    /**
     * Writes a formatted line.
     */
    public CodeWriter writeLine(String format, Object... args) {
        return writeLine(String.format(format, args));
    }

    /**
     * Writes formatted text without a newline.
     */
    public CodeWriter write(String format, Object... args) {
        return write(String.format(format, args));
    }

    /**
     * Writes an empty line.
     */
    public CodeWriter newLine() {
        buffer.append("\n");
        atLineStart = true;
        return this;
    }

    /**
     * Increases indentation.
     */
    public CodeWriter indent() {
        indentStack.push(currentIndent);
        currentIndent = currentIndent + indentString;
        return this;
    }

    /**
     * Decreases indentation.
     */
    public CodeWriter dedent() {
        if (!indentStack.isEmpty()) {
            currentIndent = indentStack.pop();
        }
        return this;
    }

    /**
     * Opens a block with a brace.
     */
    public CodeWriter openBlock(String line) {
        writeLine(line + " {");
        indent();
        return this;
    }

    /**
     * Closes a block with a brace.
     */
    public CodeWriter closeBlock() {
        dedent();
        writeLine("}");
        return this;
    }

    /**
     * Closes a block with additional text.
     */
    public CodeWriter closeBlock(String suffix) {
        dedent();
        writeLine("}" + suffix);
        return this;
    }

    /**
     * Writes a documentation comment block.
     */
    public CodeWriter writeDocComment(String doc, String prefix) {
        if (doc == null || doc.isEmpty()) {
            return this;
        }
        String[] lines = doc.split("\n");
        if (prefix.equals("///")) {
            // Rust-style doc comments
            for (String line : lines) {
                writeLine("/// " + line.trim());
            }
        } else if (prefix.equals("//")) {
            // Go-style doc comments
            for (String line : lines) {
                writeLine("// " + line.trim());
            }
        } else {
            // Java/C++ style doc comments
            writeLine("/**");
            for (String line : lines) {
                writeLine(" * " + line.trim());
            }
            writeLine(" */");
        }
        return this;
    }

    /**
     * Writes a Java-style doc comment.
     */
    public CodeWriter writeJavaDoc(String doc) {
        return writeDocComment(doc, "/**");
    }

    private void writeIndentIfNeeded() {
        if (atLineStart && !currentIndent.isEmpty()) {
            buffer.append(currentIndent);
            atLineStart = false;
        }
    }

    @Override
    public String toString() {
        return buffer.toString();
    }

    /**
     * Writes the buffer contents to a file.
     */
    public void writeToFile(Path path) throws IOException {
        Files.createDirectories(path.getParent());
        Files.writeString(path, buffer.toString());
    }
}
