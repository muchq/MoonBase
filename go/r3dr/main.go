package main

import (
	_ "github.com/go-sql-driver/mysql"
	"log"
	"net/http"
)

func main() {
	config := ReadConfig()
	shortDB := NewShortDB(config)
	shortener := NewShortener(shortDB)
	shortenerApi := NewShortenerApi(shortener)
	defer shortenerApi.Close()

	router := http.NewServeMux()
	router.HandleFunc("GET /ping", PingHandler)

	router.HandleFunc("POST /shorten", shortenerApi.ShortenHandler)
	router.HandleFunc("GET /r/{slug}", shortenerApi.RedirectHandler)

	log.Fatal(http.ListenAndServe(":"+config.Port, router))
}
