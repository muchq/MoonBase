package main

import (
	"errors"
	"github.com/muchq/moonbase/domains/platform/libs/clock"
	"strings"
	"time"
)

type ShortenRequest struct {
	LongUrl   string `json:"longUrl"`
	ExpiresAt int64  `json:"expiresAt"`
}

type ShortenResponse struct {
	Slug string `json:"slug"`
}

func ValidateShortenRequest(request ShortenRequest, clock clock.Clock) error {
	expireTime := time.UnixMilli(request.ExpiresAt)
	if expireTime.Before(clock.Now()) {
		return errors.New("expiration time is in the past")
	}
	if expireTime.After(clock.Now().Add(time.Hour * 24 * 31)) {
		return errors.New("max URL lifetime is 30 days")
	}
	if request.LongUrl == "" {
		return errors.New("longUrl is required")
	}
	if !(strings.HasPrefix(request.LongUrl, "http://") || strings.HasPrefix(request.LongUrl, "https://")) {
		// No s3 urls for now?
		return errors.New("longUrl must include protocol")
	}
	if len(request.LongUrl) < 11 {
		// shortest allowed url is like http://g.co
		return errors.New("longUrl too short")
	}
	if len(request.LongUrl) > 1000 {
		return errors.New("max url length is 1000 chars")
	}
	return nil
}
