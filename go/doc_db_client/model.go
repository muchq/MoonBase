package doc_db_client

import "github.com/muchq/moonbase/protos/doc_db"

type DocIdAndVersion struct {
	Id      string
	Version string
}

type Doc struct {
	Id      string
	Version string
	Bytes   []byte
	Tags    map[string]string
}

type DocEgg struct {
	Bytes []byte
	Tags  map[string]string
}

type DocResponse interface {
	GetDoc() *doc_db.Document
}

type DocIdResponse interface {
	GetId() string
	GetVersion() string
}
