package main

import (
	"encoding/json"
	"log"
	"net/http"
)

type ShortenerApi struct {
	shortener *Shortener
}

func NewShortenerApi(shortener *Shortener) *ShortenerApi {
	return &ShortenerApi{shortener}
}

func readBody(r *http.Request) (ShortenRequest, error) {
	decoder := json.NewDecoder(r.Body)
	var shortenRequest ShortenRequest
	err := decoder.Decode(&shortenRequest)
	return shortenRequest, err
}

func (api *ShortenerApi) ShortenHandler(w http.ResponseWriter, r *http.Request) {
	shortenRequest, err := readBody(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	response, err := api.shortener.Shorten(shortenRequest)

	if err != nil {
		log.Println(err)
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusCreated)
	_ = json.NewEncoder(w).Encode(response)
}

func (api *ShortenerApi) RedirectHandler(w http.ResponseWriter, r *http.Request) {
	slug := r.PathValue("slug")
	target, err := api.shortener.Redirect(slug)
	if err != nil {
		http.NotFound(w, r)
	} else {
		http.Redirect(w, r, target, http.StatusFound)
	}
}

func (api *ShortenerApi) Close() {
	api.shortener.Close()
}
