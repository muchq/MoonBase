package main

import (
	"errors"
	"log"
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
	if len(request.LongUrl) > 1000 {
		return ShortenResponse{}, errors.New("max url length is 1000 chars")
	}
	if request.ExpiresAt != 0 && request.ExpiresAt < time.Now().UnixMilli() {
		return ShortenResponse{}, errors.New("expiresAt is less then now")
	}
	slug, err := s.urlDao.InsertUrl(request.LongUrl, request.ExpiresAt)
	if err != nil {
		log.Println(err)
		return ShortenResponse{}, errors.New(InternalError)
	}
	return ShortenResponse{Slug: slug}, err
}

func (s *Shortener) Redirect(slug string) (string, error) {
	if len(slug) < 2 {
		return "", errors.New("invalid slug")
	}
	target, err := s.urlDao.GetLongUrl(slug)
	if err == nil && target == "" {
		return "", errors.New(NotFound)
	} else if err != nil {
		log.Println(err)
		return "", errors.New(InternalError)
	}

	return target, nil
}

func (s *Shortener) Close() {
	s.urlDao.Close()
}
