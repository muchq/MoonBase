package com.muchq.mcpserver;

import io.micronaut.context.annotation.Bean;
import io.micronaut.context.annotation.Factory;
import jakarta.inject.Singleton;

import java.time.Clock;

@Factory
public class ClockFactory {
    @Singleton
    @Bean
    public Clock clock() {
        return Clock.systemUTC();
    }
}
