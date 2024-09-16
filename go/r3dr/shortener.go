package main

import (
	"errors"
	"strings"
	"time"
)

type Shortener struct {
	urlDao UrlDao
}

func NewShortener(urlDao UrlDao) *Shortener {
	return &Shortener{urlDao}
}

func (s *Shortener) Shorten(request ShortenRequest) (ShortenResponse, error) {
	if !(strings.HasPrefix(request.LongUrl, "http://") || strings.HasPrefix(request.LongUrl, "https://")) {
		return ShortenResponse{}, errors.New("longUrl must include protocol")
	}
	if len(request.LongUrl) < 11 {
		// shortest allowed url is like http://g.co
		return ShortenResponse{}, errors.New("longUrl too short")
	}
	if request.ExpiresAt != 0 && request.ExpiresAt < time.Now().UnixMilli() {
		return ShortenResponse{}, errors.New("expiresAt is less then now")
	}
	slug, err := s.urlDao.InsertUrl(request.LongUrl, request.ExpiresAt)
	return ShortenResponse{Slug: slug}, err
}

func (s *Shortener) Redirect(slug string) (string, error) {
	return s.urlDao.GetLongUrl(slug)
}

func (s *Shortener) Close() {
	s.urlDao.Close()
}
