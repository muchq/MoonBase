package mucks

import (
	"encoding/json"
	"net/http"
)

type Mucks struct {
	Mux         *http.ServeMux
	HandlerFunc http.HandlerFunc
}

func NotFoundHandleFunc(w http.ResponseWriter, _ *http.Request) {
	JsonError(w, NewNotFound())
}

func NewMucks() *Mucks {
	m := http.NewServeMux()
	m.HandleFunc("/", NotFoundHandleFunc)
	return &Mucks{
		Mux:         m,
		HandlerFunc: m.ServeHTTP,
	}
}

func NewJsonMucks() *Mucks {
	m := NewMucks()
	m.Add(NewJsonContentTypeMiddleware())
	return m
}

func (m *Mucks) Add(middleware Middleware) {
	m.HandlerFunc = middleware.Wrap(m.HandlerFunc)
}

func (m *Mucks) HandleFunc(pattern string, handler func(http.ResponseWriter, *http.Request)) {
	m.Mux.HandleFunc(pattern, handler)
}

func (m *Mucks) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	m.HandlerFunc(w, r)
}

const ContentType = "Content-Type"
const ApplicationJsonContentType = "application/json; charset=utf-8"

// JsonError sends a JSON encoding problem response along with its associated
// status code. It DOES set the Content-Type header.
func JsonError(w http.ResponseWriter, problem Problem) {
	w.Header().Set(ContentType, ApplicationJsonContentType)
	w.WriteHeader(problem.Status)
	_ = json.NewEncoder(w).Encode(problem)
}
