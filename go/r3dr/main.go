package main

import (
	"net/http"
)

func main() {
	config := ReadConfig()
	router := http.NewServeMux()

	router.HandleFunc("GET /ping", PingHandler)

	router.HandleFunc("POST /shorten", ShortenHandler)

	http.ListenAndServe(":"+config.port, router)
}
