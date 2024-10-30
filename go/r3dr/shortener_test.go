package main

import (
	"errors"
	"github.com/hashicorp/golang-lru/v2/expirable"
	"github.com/hashicorp/golang-lru/v2/simplelru"
	"github.com/stretchr/testify/assert"
	"testing"
	"time"
)

func TestShortenWorksOnValidUrl(t *testing.T) {
	// Arrange
	shortener := NewShortener(NewTestDao(1), NewCache())
	request := ShortenRequest{LongUrl: "https://www.smallcat.dog"}

	// Act
	response, err := shortener.Shorten(request)

	// Assert
	assert.Nil(t, err, "smallcat.dog should be valid")
	assert.Equal(t, "slug", response.Slug)
}

func TestShortenReturnsErrorIfUrlDaoFails(t *testing.T) {
	// Arrange
	shortener := NewShortener(NewTestDao(0), NewCache())
	request := ShortenRequest{LongUrl: "http://g.co"}

	// Act
	_, err := shortener.Shorten(request)

	// Assert
	assert.EqualError(t, err, "internal error")
}

func TestRedirectReturnsErrorIfSlugIsTooShort(t *testing.T) {
	// Arrange
	shortener := NewShortener(NewTestDao(1), NewCache())

	// Act
	_, err := shortener.Redirect("s")

	// Assert
	assert.EqualError(t, err, "invalid slug", "slug must be at least 2 chars")
}

func TestRedirectReturnsErrorIfUrlDaoIsBroken(t *testing.T) {
	// Arrange
	shortener := NewShortener(NewTestDao(0), NewCache())

	// Act
	_, err := shortener.Redirect("slug")

	// Assert
	assert.EqualError(t, err, "internal error", "db errors should not bubble to users")
}

func TestSecondCallFailsForTestDaoWithOneCall(t *testing.T) {
	// Arrange
	shortener := NewShortener(NewTestDao(1), nil)
	request := ShortenRequest{LongUrl: "http://g.co"}

	// Act
	res1, err1 := shortener.Shorten(request)
	_, err2 := shortener.Shorten(request)

	// Assert
	assert.Nil(t, err1, "first call should succeed")
	assert.Equal(t, "slug", res1.Slug)

	assert.EqualError(t, err2, "internal error", "second call should fail")
}

func TestSecondCallHitsCache(t *testing.T) {
	// Arrange
	shortener := NewShortener(NewTestDao(1), NewCache())

	// Act
	target1, err1 := shortener.Redirect("slug")
	target2, err2 := shortener.Redirect("slug")

	// Assert
	assert.Nil(t, err1, "first call should succeed")
	assert.Equal(t, "https://www.smallcat.dog", target1)

	assert.Nil(t, err2, "second call should not hit failing DAO")
	assert.Equal(t, "https://www.smallcat.dog", target2)
}

type TestUrlDao struct {
	WorkingCalls int
}

func (d *TestUrlDao) InsertUrl(longUrl string, expiresAt int64) (string, error) {
	if d.WorkingCalls <= 0 {
		return "", errors.New("broken")
	}
	d.WorkingCalls -= 1
	return "slug", nil
}

func (d *TestUrlDao) GetLongUrl(slug string) (string, error) {
	if d.WorkingCalls <= 0 {
		return "", errors.New("broken")
	}
	d.WorkingCalls -= 1
	return "https://www.smallcat.dog", nil
}

func (*TestUrlDao) Close() {}

func NewTestDao(callsBeforeError int) UrlDao {
	return &TestUrlDao{WorkingCalls: callsBeforeError}
}

func NewCache() simplelru.LRUCache[string, string] {
	return expirable.NewLRU[string, string](2, nil, time.Minute)
}
