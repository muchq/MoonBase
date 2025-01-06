package lib

import (
	"database/sql"
)

type DaoInterface interface {
}

type Dao struct {
	db *sql.DB
}
