package com.moonbase.golfuf;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {

    private final GolfWebSocketHandler golfWebSocketHandler;

    public WebSocketConfig(GolfWebSocketHandler golfWebSocketHandler) {
        this.golfWebSocketHandler = golfWebSocketHandler;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(golfWebSocketHandler, "/ws/golf").setAllowedOrigins("*"); // Adjust allowed origins for production
    }
}
