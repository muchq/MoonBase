package main

import (
	"log"
	"net/http"
)

func main() {
	config := ReadConfig()
	router := http.NewServeMux()

	router.HandleFunc("GET /ping", PingHandler)

	router.HandleFunc("POST /shorten", ShortenHandler)

	router.HandleFunc("GET /r/{slug}", RedirectHandler)

	log.Fatal(http.ListenAndServe(":"+config.port, router))
}
