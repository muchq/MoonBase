package main

import (
	"encoding/json"
	"log"
	"net/http"
)

type ShortenerService struct {
	shortDb *ShortDB
}

func NewShortenerService(shortDb *ShortDB) *ShortenerService {
	return &ShortenerService{shortDb: shortDb}
}

func readBody(r *http.Request) (ShortenRequest, error) {
	decoder := json.NewDecoder(r.Body)
	var shortenRequest ShortenRequest
	err := decoder.Decode(&shortenRequest)
	return shortenRequest, err
}

func (svc *ShortenerService) ShortenHandler(w http.ResponseWriter, r *http.Request) {
	shortenRequest, err := readBody(r)
	if err != nil {
		log.Println(err)
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	slug, err := svc.shortDb.InsertUrl(shortenRequest.LongUrl, shortenRequest.ExpiresAt)
	if err != nil {
		log.Println(err)
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}

	shortenResponse := ShortenResponse{slug}
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusCreated)
	_ = json.NewEncoder(w).Encode(shortenResponse)
}

func (svc *ShortenerService) RedirectHandler(w http.ResponseWriter, r *http.Request) {
	slug := r.PathValue("slug")
	target, err := svc.shortDb.GetLongUrl(slug)
	if err != nil {
		http.NotFound(w, r)
	} else {
		http.Redirect(w, r, target, http.StatusFound)
	}
}

func (svc *ShortenerService) Close() {
	svc.shortDb.Close()
}
