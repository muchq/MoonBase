package escapist_client

import "github.com/muchq/moonbase/protos/escapist"

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
	GetDoc() *escapist.Document
}

type DocIdResponse interface {
	GetId() string
	GetVersion() string
}
