package main

import (
	"errors"
	"github.com/muchq/moonbase/go/clock"
	"github.com/stretchr/testify/assert"
	"testing"
	"time"
)

func TestShortenErrorOnEmptyLongUrl(t *testing.T) {
	// Arrange
	testClock := clock.NewTestClock()
	request := ShortenRequest{
		LongUrl:   "",
		ExpiresAt: testClock.Now().Add(time.Hour * 24).UnixMilli(),
	}

	// Act
	err := ValidateShortenRequest(request, testClock)

	// Assert
	assert.Equal(t, errors.New("longUrl is required"), err)
}

func TestShortenErrorOnLongUrlWithoutProtocol(t *testing.T) {
	// Arrange
	testClock := clock.NewTestClock()
	request := ShortenRequest{
		LongUrl:   "google.com",
		ExpiresAt: testClock.Now().Add(time.Hour * 24).UnixMilli(),
	}

	// Act
	err := ValidateShortenRequest(request, testClock)

	// Assert
	assert.Equal(t, errors.New("longUrl must include protocol"), err)
}

func TestShortenErrorOnLongUrlTooShort(t *testing.T) {
	// Arrange
	testClock := clock.NewTestClock()
	request := ShortenRequest{
		LongUrl:   "http://g.c",
		ExpiresAt: testClock.Now().Add(time.Hour * 24).UnixMilli(),
	}

	// Act
	err := ValidateShortenRequest(request, testClock)

	// Assert
	assert.Equal(t, errors.New("longUrl too short"), err)
}

func TestShortenErrorLongUrlTooLong(t *testing.T) {
	// Arrange
	runes := make([]rune, 985)
	for i := range runes {
		runes[i] = 'a'
	}
	queryString := "?foo=" + string(runes)
	testClock := clock.NewTestClock()
	request := ShortenRequest{
		LongUrl:   "http://g.co" + queryString,
		ExpiresAt: testClock.Now().Add(time.Hour * 24).UnixMilli(),
	}

	// Act
	err := ValidateShortenRequest(request, testClock)

	// Assert
	assert.Equal(t, errors.New("max url length is 1000 chars"), err)
}

func TestShortenErrorOnNegativeExpiresAt(t *testing.T) {
	// Arrange
	testClock := clock.NewTestClock()
	testClock.Tick(1_000_000)
	request := ShortenRequest{
		LongUrl:   "http://g.co",
		ExpiresAt: -129,
	}

	// Act
	err := ValidateShortenRequest(request, testClock)

	// Assert
	assert.Equal(t, errors.New("expiration time is in the past"), err)
}

func TestShortenErrorOnExpiresAtInThePast(t *testing.T) {
	// Arrange
	testClock := clock.NewTestClock()
	testClock.Tick(1_000_000)
	request := ShortenRequest{
		LongUrl:   "http://g.co",
		ExpiresAt: 999_999,
	}

	// Act
	err := ValidateShortenRequest(request, testClock)

	// Assert
	assert.Equal(t, errors.New("expiration time is in the past"), err)
}
