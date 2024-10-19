package mucks

import (
	"encoding/json"
	"github.com/google/uuid"
	"net/http"
)

type Middleware interface {
	Wrap(handlerFunc http.HandlerFunc) http.HandlerFunc
}

type Mucks struct {
	Mux         *http.ServeMux
	HandlerFunc http.HandlerFunc
}

var NotFoundResponse Problem = Problem{
	Status:    404,
	ErrorCode: 404,
	Message:   "Not Found",
}

func NotFoundHandleFunc(w http.ResponseWriter, _ *http.Request) {
	problem := NotFoundResponse
	problem.Instance = uuid.NewString()
	jsonError(w, problem)
}

func NewMucks() *Mucks {
	m := http.NewServeMux()
	m.HandleFunc("/", NotFoundHandleFunc)
	return &Mucks{
		Mux:         m,
		HandlerFunc: m.ServeHTTP,
	}
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

// This is sorta similar to the http problem spec...
type Problem struct {
	Status    int    `json:"status"`
	ErrorCode int    `json:"errorCode"`
	Message   string `json:"message"`
	Instance  string `json:"instance"`
}

const ContentType = "Content-Type"
const ApplicationJsonContentType = "application/json; charset=utf-8"

func jsonError(w http.ResponseWriter, problem Problem) {
	w.Header().Set(ContentType, ApplicationJsonContentType)
	w.WriteHeader(problem.Status)
	json.NewEncoder(w).Encode(problem)
}
