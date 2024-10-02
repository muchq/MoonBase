package main

import (
	"log"
	"os"
	"strings"
)

type Config struct {
	Port             string
	ConnectionString string
}

func ReadConnectionString() (string, bool) {
	// First try env
	connectionString, ok := os.LookupEnv("DB_CONNECTION_STRING")
	if ok {
		return connectionString, true
	}

	// Fall back to config file
	content, err := os.ReadFile("/etc/r3dr/db_config")
	if err != nil {
		return "", false
	}

	trimmed := strings.TrimSpace(string(content))
	if len(trimmed) == 0 {
		return "", false
	}
	return trimmed, true
}

func ReadConfig() Config {
	connectionString, ok := ReadConnectionString()
	if !ok {
		log.Fatalf("DB_CONNECTION_STRING env var not set, and no config at /etc/r3dr/db_config")
	}

	port, ok := os.LookupEnv("PORT")
	if !ok {
		log.Printf("PORT env var not set. Defaulting to 8080.")
		port = "8080"
	}

	return Config{port, connectionString}
}
