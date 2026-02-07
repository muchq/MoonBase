package mucks

import (
	"encoding/json"
	"github.com/stretchr/testify/assert"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
)

func Setup() (*Mucks, *httptest.Server, *http.Client) {
	m := NewMucks()
	s := httptest.NewServer(m)
	c := s.Client()
	return m, s, c
}

func Get(t *testing.T, url string, client *http.Client, expectedStatus int, out any) {
	response, err := client.Get(url)
	assert.Nil(t, err, "error on Get")
	assert.Equal(t, expectedStatus, response.StatusCode, "status code")

	bodyBytes, err := io.ReadAll(response.Body)
	assert.Nil(t, err, "error on ReadAll")

	_ = json.Unmarshal(bodyBytes, out)
}

func TestMucks_NotFoundDefault(t *testing.T) {
	_, s, client := Setup()
	defer s.Close()

	p := Problem{}
	Get(t, s.URL, client, 404, &p)

	assert.Equal(t, "Not Found", p.Message)
	assert.Equal(t, "Not Found", p.Detail)
	assert.Equal(t, 404, p.ErrorCode)
	assert.Equal(t, 404, p.StatusCode)
}

func TestMucks_SimpleHandler(t *testing.T) {
	m, s, client := Setup()
	defer s.Close()

	m.HandleFunc("GET /foo", FooHandler)

	var response FooResponse
	Get(t, s.URL+"/foo", client, 200, &response)

	assert.Equal(t, "bonk", response.Name)
	assert.Equal(t, 100, response.Value)
}

func TestMucks_SimpleMiddleware(t *testing.T) {
	m, s, client := Setup()
	defer s.Close()

	m.Add(&FooMiddleware{})
	m.HandleFunc("GET /foo", FooHandler)

	response, err := client.Get(s.URL + "/foo")
	assert.Nil(t, err, "error on Get")

	assert.Equal(t, "123", response.Header.Get("Foo"), "header should be set")
}

type FooResponse struct {
	Name  string `json:"name"`
	Value int    `json:"value"`
}

func FooHandler(w http.ResponseWriter, _ *http.Request) {
	foo := FooResponse{
		Name:  "bonk",
		Value: 100,
	}

	JsonOk(w, foo)
}

type FooMiddleware struct {
}

func (*FooMiddleware) Wrap(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Foo", "123")
		next(w, r)
	}
}
