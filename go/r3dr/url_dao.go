package main

type UrlDao interface {
	InsertUrl(longUrl string, expiresAt int64) (string, error)
	GetLongUrl(slug string) (string, error)
	Close()
}
