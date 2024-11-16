package doc_db_client

import (
	"context"
	"github.com/muchq/moonbase/protos/doc_db"
	"github.com/stretchr/testify/assert"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"testing"
)

func TestDocDbClient_InsertDocRpcSuccess(t *testing.T) {
	// Arrange
	docEgg := DocEgg{
		Bytes: []byte{1, 2, 3},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.InsertDoc("test", docEgg)

	// Assert
	assert.NoError(t, err, "call should have succeeded")
	assert.Equal(t, "123", res.Id)
	assert.Equal(t, "foo", res.Version)
}

func TestDocDbClient_InsertDocRpcFailure(t *testing.T) {
	// Arrange
	docEgg := DocEgg{
		Bytes: []byte{1, 2, 3},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&BrokenStub{}, "foo")

	// Act
	res, err := client.InsertDoc("test", docEgg)

	// Assert
	assert.EqualError(t, err, "rpc error: code = Canceled desc = request canceled")
	assert.Equal(t, res, DocIdAndVersion{}, "res should be zero value")
}

func TestDocDbClient_InsertDocClientValidatesCollection(t *testing.T) {
	// Arrange
	docEgg := DocEgg{
		Bytes: []byte{1, 2, 3},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.InsertDoc("", docEgg)

	// Assert
	assert.EqualError(t, err, "collection cannot be empty")
	assert.Equal(t, res, DocIdAndVersion{}, "res should be zero value")
}

func TestDocDbClient_InsertDocClientValidatesBytes(t *testing.T) {
	// Arrange
	docEgg := DocEgg{
		Bytes: []byte{},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.InsertDoc("test", docEgg)

	// Assert
	assert.EqualError(t, err, "bytes cannot be empty")
	assert.Equal(t, res, DocIdAndVersion{}, "res should be zero value")
}

func TestDocDbClient_UpdateDocRpcSuccess(t *testing.T) {
	// Arrange
	idAndVersion := DocIdAndVersion{
		Id:      "123",
		Version: "foo",
	}

	docEgg := DocEgg{
		Bytes: []byte{1, 2, 3},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.UpdateDoc("test", idAndVersion, docEgg)

	// Assert
	assert.NoError(t, err, "call should have succeeded")
	assert.Equal(t, "123", res.Id)
	assert.Equal(t, "foo_new", res.Version)
}

func TestDocDbClient_UpdateDocRpcFailure(t *testing.T) {
	// Arrange
	idAndVersion := DocIdAndVersion{
		Id:      "123",
		Version: "foo",
	}

	docEgg := DocEgg{
		Bytes: []byte{1, 2, 3},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&BrokenStub{}, "foo")

	// Act
	res, err := client.UpdateDoc("test", idAndVersion, docEgg)

	// Assert
	assert.EqualError(t, err, "rpc error: code = Canceled desc = request canceled")
	assert.Equal(t, res, DocIdAndVersion{}, "res should be zero value")
}

func TestDocDbClient_UpdateDocClientValidatesCollection(t *testing.T) {
	// Arrange
	idAndVersion := DocIdAndVersion{
		Id:      "123",
		Version: "foo",
	}

	docEgg := DocEgg{
		Bytes: []byte{1, 2, 3},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.UpdateDoc("", idAndVersion, docEgg)

	// Assert
	assert.EqualError(t, err, "collection cannot be empty")
	assert.Equal(t, res, DocIdAndVersion{}, "res should be zero value")
}

func TestDocDbClient_UpdateDocClientValidatesId(t *testing.T) {
	// Arrange
	idAndVersion := DocIdAndVersion{
		Id:      "",
		Version: "foo",
	}

	docEgg := DocEgg{
		Bytes: []byte{1, 2, 3},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.UpdateDoc("test", idAndVersion, docEgg)

	// Assert
	assert.EqualError(t, err, "id cannot be empty")
	assert.Equal(t, res, DocIdAndVersion{}, "res should be zero value")
}

func TestDocDbClient_UpdateDocClientValidatesVersion(t *testing.T) {
	// Arrange
	idAndVersion := DocIdAndVersion{
		Id:      "123",
		Version: "",
	}

	docEgg := DocEgg{
		Bytes: []byte{1, 2, 3},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.UpdateDoc("test", idAndVersion, docEgg)

	// Assert
	assert.EqualError(t, err, "version cannot be empty")
	assert.Equal(t, res, DocIdAndVersion{}, "res should be zero value")
}

func TestDocDbClient_UpdateDocClientValidatesBytes(t *testing.T) {
	// Arrange
	idAndVersion := DocIdAndVersion{
		Id:      "123",
		Version: "foo",
	}

	docEgg := DocEgg{
		Bytes: []byte{},
		Tags:  make(map[string]string),
	}

	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.UpdateDoc("test", idAndVersion, docEgg)

	// Assert
	assert.EqualError(t, err, "bytes cannot be empty")
	assert.Equal(t, res, DocIdAndVersion{}, "res should be zero value")
}

func TestDocDbClient_FindDocByIdRpcSuccess(t *testing.T) {
	// Arrange
	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.FindDocById("test", "boing")

	// Assert
	assert.NoError(t, err, "call should have succeeded")
	assert.Equal(t, "boing", res.Id)
	assert.Equal(t, "foo", res.Version)
	assert.Equal(t, []byte{1, 2, 3}, res.Bytes)
	assert.Equal(t, make(map[string]string), res.Tags)
}

func TestDocDbClient_FindDocByIdRpcFailure(t *testing.T) {
	// Arrange
	client := NewDocDbClient(&BrokenStub{}, "foo")

	// Act
	res, err := client.FindDocById("test", "boing")

	// Assert
	assert.EqualError(t, err, "rpc error: code = Canceled desc = request canceled")
	assert.Equal(t, res, Doc{}, "res should be zero value")
}

func TestDocDbClient_FindDocByIdClientValidatesCollection(t *testing.T) {
	// Arrange
	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.FindDocById("", "boing")

	// Assert
	assert.EqualError(t, err, "collection cannot be empty")
	assert.Equal(t, res, Doc{}, "res should be zero value")
}

func TestDocDbClient_FindDocByIdClientValidatesId(t *testing.T) {
	// Arrange
	client := NewDocDbClient(&WorkingStub{}, "foo")

	// Act
	res, err := client.FindDocById("test", "")

	// Assert
	assert.EqualError(t, err, "id cannot be empty")
	assert.Equal(t, res, Doc{}, "res should be zero value")
}

func TestDocDbClient_FindDocByTagsRpcSuccess(t *testing.T) {
	// Arrange
	client := NewDocDbClient(&WorkingStub{}, "foo")
	tags := map[string]string{"foo": "bar"}

	// Act
	res, err := client.FindDocByTags("test", tags)

	// Assert
	assert.NoError(t, err, "call should have succeeded")
	assert.Equal(t, "123", res.Id)
	assert.Equal(t, "foo", res.Version)
	assert.Equal(t, []byte{1, 2, 3}, res.Bytes)
	assert.Equal(t, map[string]string{"foo": "bar"}, res.Tags)
}

func TestDocDbClient_FindDocByTagsRpcFailure(t *testing.T) {
	// Arrange
	client := NewDocDbClient(&BrokenStub{}, "foo")
	tags := map[string]string{"foo": "bar"}

	// Act
	res, err := client.FindDocByTags("test", tags)

	// Assert
	assert.EqualError(t, err, "rpc error: code = Canceled desc = request canceled")
	assert.Equal(t, res, Doc{}, "res should be zero value")
}

func TestDocDbClient_FindDocByTagsClientValidatesCollection(t *testing.T) {
	// Arrange
	client := NewDocDbClient(&WorkingStub{}, "foo")
	tags := map[string]string{"foo": "bar"}

	// Act
	res, err := client.FindDocByTags("", tags)

	// Assert
	assert.EqualError(t, err, "collection cannot be empty")
	assert.Equal(t, res, Doc{}, "res should be zero value")
}

func TestDocDbClient_FindDocByTagsClientValidatesTags(t *testing.T) {
	// Arrange
	client := NewDocDbClient(&WorkingStub{}, "foo")
	tags := make(map[string]string)

	// Act
	res, err := client.FindDocByTags("test", tags)

	// Assert
	assert.EqualError(t, err, "tags cannot be empty")
	assert.Equal(t, res, Doc{}, "res should be zero value")
}

type WorkingStub struct {
}

func (*WorkingStub) InsertDoc(context.Context, *doc_db.InsertDocRequest, ...grpc.CallOption) (*doc_db.InsertDocResponse, error) {
	response := &doc_db.InsertDocResponse{
		Id:      "123",
		Version: "foo",
	}

	return response, nil
}

func (*WorkingStub) UpdateDoc(_ context.Context, req *doc_db.UpdateDocRequest, _ ...grpc.CallOption) (*doc_db.UpdateDocResponse, error) {
	response := &doc_db.UpdateDocResponse{
		Id:      req.Id,
		Version: req.Version + "_new",
	}

	return response, nil
}

func (*WorkingStub) FindDocById(_ context.Context, req *doc_db.FindDocByIdRequest, _ ...grpc.CallOption) (*doc_db.FindDocByIdResponse, error) {
	response := &doc_db.FindDocByIdResponse{
		Doc: &doc_db.Document{
			Id:      req.Id,
			Version: "foo",
			Bytes:   []byte{1, 2, 3},
			Tags:    make(map[string]string),
		},
	}

	return response, nil
}

func (*WorkingStub) FindDoc(_ context.Context, req *doc_db.FindDocRequest, _ ...grpc.CallOption) (*doc_db.FindDocResponse, error) {
	response := &doc_db.FindDocResponse{
		Doc: &doc_db.Document{
			Id:      "123",
			Version: "foo",
			Bytes:   []byte{1, 2, 3},
			Tags:    req.Tags,
		},
	}

	return response, nil
}

type BrokenStub struct {
}

func (*BrokenStub) InsertDoc(context.Context, *doc_db.InsertDocRequest, ...grpc.CallOption) (*doc_db.InsertDocResponse, error) {
	return nil, status.Error(codes.Canceled, "request canceled")
}

func (*BrokenStub) UpdateDoc(context.Context, *doc_db.UpdateDocRequest, ...grpc.CallOption) (*doc_db.UpdateDocResponse, error) {
	return nil, status.Error(codes.Canceled, "request canceled")
}

func (*BrokenStub) FindDocById(context.Context, *doc_db.FindDocByIdRequest, ...grpc.CallOption) (*doc_db.FindDocByIdResponse, error) {
	return nil, status.Error(codes.Canceled, "request canceled")
}

func (*BrokenStub) FindDoc(context.Context, *doc_db.FindDocRequest, ...grpc.CallOption) (*doc_db.FindDocResponse, error) {
	return nil, status.Error(codes.Canceled, "request canceled")
}
