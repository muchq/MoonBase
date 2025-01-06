package client

import (
	"context"
	"errors"
	"github.com/muchq/moonbase/protos/doc_db"
	"google.golang.org/grpc/metadata"
)

type DocDbClient struct {
	Namespace string
	stub      doc_db.DocDbClient
}

func NewDocDbClient(stub doc_db.DocDbClient, namespace string) *DocDbClient {
	return &DocDbClient{
		Namespace: namespace,
		stub:      stub,
	}
}

func (c *DocDbClient) InsertDoc(collection string, inputDocEgg DocEgg) (DocIdAndVersion, error) {
	if collection == "" {
		return DocIdAndVersion{}, errors.New("collection cannot be empty")
	}
	if len(inputDocEgg.Bytes) == 0 {
		return DocIdAndVersion{}, errors.New("bytes cannot be empty")
	}

	request := &doc_db.InsertDocRequest{
		Collection: collection,
		Doc:        makeDocEggProto(inputDocEgg),
	}

	res, err := c.stub.InsertDoc(c.makeContext(), request)
	return handleDocIdResponse(res, err)
}

func (c *DocDbClient) UpdateDoc(collection string, idAndVersion DocIdAndVersion, inputDocEgg DocEgg) (DocIdAndVersion, error) {
	if collection == "" {
		return DocIdAndVersion{}, errors.New("collection cannot be empty")
	}
	if idAndVersion.Id == "" {
		return DocIdAndVersion{}, errors.New("id cannot be empty")
	}
	if idAndVersion.Version == "" {
		return DocIdAndVersion{}, errors.New("version cannot be empty")
	}
	if len(inputDocEgg.Bytes) == 0 {
		return DocIdAndVersion{}, errors.New("bytes cannot be empty")
	}

	request := &doc_db.UpdateDocRequest{
		Collection: collection,
		Id:         idAndVersion.Id,
		Version:    idAndVersion.Version,
		Doc:        makeDocEggProto(inputDocEgg),
	}

	res, err := c.stub.UpdateDoc(c.makeContext(), request)
	return handleDocIdResponse(res, err)
}

func (c *DocDbClient) FindDocById(collection string, id string) (Doc, error) {
	if collection == "" {
		return Doc{}, errors.New("collection cannot be empty")
	}
	if id == "" {
		return Doc{}, errors.New("id cannot be empty")
	}

	request := &doc_db.FindDocByIdRequest{
		Collection: collection,
		Id:         id,
	}

	res, err := c.stub.FindDocById(c.makeContext(), request)
	return handleDocResponse(res, err)
}

func (c *DocDbClient) FindDocByTags(collection string, tags map[string]string) (Doc, error) {
	if collection == "" {
		return Doc{}, errors.New("collection cannot be empty")
	}
	if len(tags) == 0 {
		return Doc{}, errors.New("tags cannot be empty")
	}

	request := &doc_db.FindDocRequest{
		Collection: collection,
		Tags:       tags,
	}

	res, err := c.stub.FindDoc(c.makeContext(), request)
	return handleDocResponse(res, err)
}

func (c *DocDbClient) makeContext() context.Context {
	return metadata.AppendToOutgoingContext(context.Background(), "db_namespace", c.Namespace)
}

func handleDocResponse(response DocResponse, err error) (Doc, error) {
	var doc Doc
	if err == nil {
		_doc := response.GetDoc()
		doc.Id = _doc.Id
		doc.Version = _doc.Version
		doc.Bytes = _doc.Bytes
		doc.Tags = _doc.Tags
	}

	return doc, err
}

func handleDocIdResponse(response DocIdResponse, err error) (DocIdAndVersion, error) {
	var docIdAndVersion DocIdAndVersion
	if err == nil {
		docIdAndVersion.Id = response.GetId()
		docIdAndVersion.Version = response.GetVersion()
	}
	return docIdAndVersion, err
}

func makeDocEggProto(inputDocEgg DocEgg) *doc_db.DocumentEgg {
	return &doc_db.DocumentEgg{
		Bytes: inputDocEgg.Bytes,
		Tags:  inputDocEgg.Tags,
	}
}
