package main

type ShortenRequest struct {
	LongUrl   string `json:"longUrl"`
	ExpiresAt int64  `json:"expiresAt"`
}

type ShortenResponse struct {
	Slug string `json:"slug"`
}
