package main

import (
	"log"
	"net/http"
)

func ShortenHandler(w http.ResponseWriter, r *http.Request) {
}

func RedirectHandler(w http.ResponseWriter, r *http.Request) {
	slug := r.PathValue("slug")
	log.Println("got slug", slug)
}
