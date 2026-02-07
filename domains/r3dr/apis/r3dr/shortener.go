package main

import (
	"errors"
	"github.com/hashicorp/golang-lru/v2/simplelru"
	"log"
	"time"
)

type ShortenerCacheConfig struct {
	MaxItems          int
	ExpirationMinutes time.Duration
}

type Shortener struct {
	cache  simplelru.LRUCache[string, string]
	urlDao UrlDao
}

func NewShortener(urlDao UrlDao, cache simplelru.LRUCache[string, string]) *Shortener {
	return &Shortener{cache: cache, urlDao: urlDao}
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

	target, found := s.cache.Get(slug)
	var err error
	if !found {
		target, err = s.urlDao.GetLongUrl(slug)
		if err == nil && target == "" {
			return "", errors.New(NotFound)
		} else if err != nil {
			log.Println(err)
			return "", errors.New(InternalError)
		}
		s.cache.Add(slug, target)
	}

	return target, nil
}

func (s *Shortener) Close() {
	s.urlDao.Close()
}
