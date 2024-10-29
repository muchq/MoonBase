package main

import (
	"errors"
	"testing"
)

func TestShortenWorksOnValidUrl(t *testing.T) {
	// Arrange
	shortener := NewShortener(WorkingUrlDao{})
	request := ShortenRequest{LongUrl: "https://www.smallcat.dog"}

	// Act
	response, err := shortener.Shorten(request)

	// Assert
	if err != nil {
		t.Error("smallcat.dog should be valid")
	}

	if response.Slug != "slug" {
		t.Error("expected fake slug in response")
	}
}

func TestShortenReturnsErrorIfUrlDaoFails(t *testing.T) {
	// Arrange
	shortener := NewShortener(BrokenUrlDao{})
	request := ShortenRequest{LongUrl: "http://g.co"}

	// Act
	_, err := shortener.Shorten(request)

	// Assert
	if err == nil || err.Error() != "internal error" {
		t.Error("db errors should not bubble to users")
	}
}

func TestRedirectReturnsErrorIfSlugIsTooShort(t *testing.T) {
	// Arrange
	shortener := NewShortener(WorkingUrlDao{})

	// Act
	_, err := shortener.Redirect("s")

	// Assert
	if err == nil || err.Error() != "invalid slug" {
		t.Error("slug must be at least 2 chars")
	}
}

func TestRedirectReturnsErrorIfUrlDaoIsBroken(t *testing.T) {
	// Arrange
	shortener := NewShortener(BrokenUrlDao{})

	// Act
	_, err := shortener.Redirect("slug")

	// Assert
	if err == nil || err.Error() != "internal error" {
		t.Error("db errors should not bubble to users")
	}
}

type WorkingUrlDao struct{}

func (WorkingUrlDao) InsertUrl(longUrl string, expiresAt int64) (string, error) {
	return "slug", nil
}

func (WorkingUrlDao) GetLongUrl(slug string) (string, error) {
	return "https://www.smallcat.dog", nil
}

func (WorkingUrlDao) Close() {}

type BrokenUrlDao struct{}

func (BrokenUrlDao) InsertUrl(longUrl string, expiresAt int64) (string, error) {
	return "", errors.New("broken")
}

func (BrokenUrlDao) GetLongUrl(slug string) (string, error) {
	return "", errors.New("broken")
}

func (BrokenUrlDao) Close() {}
