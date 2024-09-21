package main

import (
	"errors"
	"testing"
	"time"
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

func TestShortenErrorOnEmptyLongUrl(t *testing.T) {
	// Arrange
	shortener := NewShortener(WorkingUrlDao{})
	request := ShortenRequest{LongUrl: ""}

	// Act
	_, err := shortener.Shorten(request)

	// Assert
	if err == nil {
		t.Error("shortener should return error on empty LongUrl")
	}
}

func TestShortenErrorOnLongUrlWithoutProtocol(t *testing.T) {
	// Arrange
	shortener := NewShortener(WorkingUrlDao{})
	request := ShortenRequest{LongUrl: "google.com"}

	// Act
	_, err := shortener.Shorten(request)

	// Assert
	if err == nil || err.Error() != "longUrl must include protocol" {
		t.Error("protocol should be required")
	}
}

func TestShortenErrorOnLongUrlTooShort(t *testing.T) {
	// Arrange
	shortener := NewShortener(WorkingUrlDao{})
	request := ShortenRequest{LongUrl: "http://g.c"}

	// Act
	_, err := shortener.Shorten(request)

	// Assert
	if err == nil || err.Error() != "longUrl too short" {
		t.Error("LongUrl must be 11 chars or longer")
	}
}

func TestShortenErrorLongUrlTooLong(t *testing.T) {
	// Arrange
	shortener := NewShortener(WorkingUrlDao{})
	runes := make([]rune, 985)
	for i := range runes {
		runes[i] = 'a'
	}
	queryString := "?foo=" + string(runes)
	request := ShortenRequest{LongUrl: "http://g.co" + queryString}

	// Act
	_, err := shortener.Shorten(request)

	// Assert
	if err == nil || err.Error() != "max url length is 1000 chars" {
		t.Error("max LongUrl length is 1000 chars")
	}
}

func TestShortenErrorOnNegativeExpiresAt(t *testing.T) {
	// Arrange
	shortener := NewShortener(WorkingUrlDao{})
	request := ShortenRequest{LongUrl: "http://g.co", ExpiresAt: -129}

	// Act
	_, err := shortener.Shorten(request)

	// Assert
	if err == nil || err.Error() != "expiresAt is less then now" {
		t.Error("negative ExpiresAt is not allowed")
	}
}

func TestShortenErrorOnExpiresAtInThePast(t *testing.T) {
	// Arrange
	nowMillis := time.Now().UnixMilli()
	shortener := NewShortener(WorkingUrlDao{})
	request := ShortenRequest{LongUrl: "http://g.co", ExpiresAt: nowMillis - 10_000}

	// Act
	_, err := shortener.Shorten(request)

	// Assert
	if err == nil || err.Error() != "expiresAt is less then now" {
		t.Error("ExpiresAt in the past is not allowed")
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
