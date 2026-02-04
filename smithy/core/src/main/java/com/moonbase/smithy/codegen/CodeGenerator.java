package com.moonbase.smithy.codegen;

import com.moonbase.smithy.model.SmithyModel;
import java.io.IOException;
import java.nio.file.Path;
import java.util.Map;

/**
 * Base interface for code generators.
 */
public interface CodeGenerator {

    /**
     * Returns the target language name.
     */
    String getLanguage();

    /**
     * Generates code from a Smithy model.
     *
     * @param model The parsed Smithy model
     * @param outputDir The output directory for generated files
     * @param options Generator options
     * @throws IOException If generation fails
     */
    void generate(SmithyModel model, Path outputDir, GeneratorOptions options) throws IOException;

    /**
     * Options for code generation.
     */
    class GeneratorOptions {
        private String packageName;
        private String moduleName;
        private boolean generateClient = false;
        private boolean generateServer = true;
        private boolean generateTests = true;
        private String protocol = "restJson1";
        private Map<String, String> customOptions = Map.of();

        public String getPackageName() {
            return packageName;
        }

        public GeneratorOptions setPackageName(String packageName) {
            this.packageName = packageName;
            return this;
        }

        public String getModuleName() {
            return moduleName;
        }

        public GeneratorOptions setModuleName(String moduleName) {
            this.moduleName = moduleName;
            return this;
        }

        public boolean isGenerateClient() {
            return generateClient;
        }

        public GeneratorOptions setGenerateClient(boolean generateClient) {
            this.generateClient = generateClient;
            return this;
        }

        public boolean isGenerateServer() {
            return generateServer;
        }

        public GeneratorOptions setGenerateServer(boolean generateServer) {
            this.generateServer = generateServer;
            return this;
        }

        public boolean isGenerateTests() {
            return generateTests;
        }

        public GeneratorOptions setGenerateTests(boolean generateTests) {
            this.generateTests = generateTests;
            return this;
        }

        public String getProtocol() {
            return protocol;
        }

        public GeneratorOptions setProtocol(String protocol) {
            this.protocol = protocol;
            return this;
        }

        public Map<String, String> getCustomOptions() {
            return customOptions;
        }

        public GeneratorOptions setCustomOptions(Map<String, String> customOptions) {
            this.customOptions = customOptions;
            return this;
        }
    }
}
