package main

import (
	_ "github.com/go-sql-driver/mysql"
	"log"
	"net/http"
)

func main() {
	config := ReadConfig()
	shortDB := NewShortDB(config)
	shortenerService := NewShortenerService(shortDB)
	defer shortenerService.Close()

	router := http.NewServeMux()
	router.HandleFunc("GET /ping", PingHandler)

	router.HandleFunc("POST /shorten", shortenerService.ShortenHandler)
	router.HandleFunc("GET /r/{slug}", shortenerService.RedirectHandler)

	log.Fatal(http.ListenAndServe(":"+config.Port, router))
}
