package main

import (
	"log"
	"os"
)

type Config struct {
	port             string
	connectionString string
}

func ReadConfig() Config {
	connectionString, ok := os.LookupEnv("MYSQL_CONNECTION_STRING")
	if !ok {
		log.Fatalf("MYSQL_CONNECTION_STRING env var not set.")
	}

	port, ok := os.LookupEnv("PORT")
	if !ok {
		log.Printf("PORT env var not set. Defaulting to 8080.")
		port = "8080"
	}

	return Config{port, connectionString}
}
