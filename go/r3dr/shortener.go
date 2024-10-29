package main

import (
	"errors"
	"log"
)

type Shortener struct {
	urlDao UrlDao
}

func NewShortener(urlDao UrlDao) *Shortener {
	return &Shortener{urlDao}
}

func (s *Shortener) Shorten(request ShortenRequest) (ShortenResponse, error) {
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
