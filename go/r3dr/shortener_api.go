package main

import (
	"encoding/json"
	"github.com/muchq/moonbase/go/mucks"
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
		mucks.JsonError(w, mucks.NewBadRequest(err.Error()))
		return
	}

	response, err := api.shortener.Shorten(shortenRequest)

	if err != nil {
		if err.Error() == InternalError {
			mucks.JsonError(w, mucks.NewServerError(500))
		} else {
			mucks.JsonError(w, mucks.NewBadRequest(err.Error()))
		}
		return
	}

	w.Header().Set(mucks.ContentType, mucks.ApplicationJsonContentType)
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
