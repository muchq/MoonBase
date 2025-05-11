package com.moonbase.golfuf;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.ResourceHandlerRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

@Configuration
public class StaticResourceConfig implements WebMvcConfigurer {

    @Override
    public void addResourceHandlers(ResourceHandlerRegistry registry) {
        // This allows serving static files (like index.html, script.js, styles.css)
        // directly from the classpath root if they are not found in the default /static path.
        // The Bazel build places resources from //web/golf_ui_2:golf_ui_2_static_files
        // into the classpath root.
        registry.addResourceHandler("/**")
                .addResourceLocations("classpath:/")
                .setCachePeriod(3600) // Cache for 1 hour, adjust as needed
                .resourceChain(true);

        // Default Spring Boot static resource locations are:
        // classpath:/META-INF/resources/, classpath:/resources/, classpath:/static/, classpath:/public/
        // This handler is added to catch files at classpath:/ as a fallback.
    }
}
