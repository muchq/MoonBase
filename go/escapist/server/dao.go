package server

import (
	"database/sql"
	_ "github.com/lib/pq"
)

type DaoInterface interface {
}

type Dao struct {
	db *sql.DB
}
