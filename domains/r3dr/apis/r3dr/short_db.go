package main

import (
	"database/sql"
	"fmt"
	_ "github.com/lib/pq"
	"log"
	"time"
)

type ShortDB struct {
	db *sql.DB
}

func NewShortDB(config Config) *ShortDB {
	db, err := sql.Open("postgres", config.ConnectionString)
	if err != nil {
		log.Fatalf("could not connect to database: %v", err)
	}
	return &ShortDB{db: db}
}

func (d *ShortDB) InsertUrl(longUrl string, expiresAt int64) (string, error) {
	r := d.db.QueryRow("SELECT nextval('url_ids')")
	var newId int64
	idErr := r.Scan(&newId)
	if idErr != nil {
		return "", idErr
	}

	slug, _ := EncodeId(newId)
	expireTime := time.UnixMilli(expiresAt)

	_, execErr := d.db.Exec("INSERT INTO urls (id, long_url, short_url, expires_at) VALUES($1, $2, $3, $4);", newId, longUrl, slug, expireTime)
	if execErr != nil {
		fmt.Println("error inserting row")
		return "", execErr
	}

	return slug, nil
}

func (d *ShortDB) GetLongUrl(slug string) (string, error) {
	statement, err := d.db.Prepare("SELECT long_url FROM urls WHERE short_url = $1")
	if err != nil {
		return "", err
	}
	defer statement.Close()

	var target string
	err = statement.QueryRow(slug).Scan(&target)
	if err == sql.ErrNoRows {
		return "", nil
	}
	return target, err
}

func (d *ShortDB) Close() {
	d.db.Close()
}
