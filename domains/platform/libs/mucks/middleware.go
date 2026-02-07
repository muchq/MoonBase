package mucks

import "net/http"

type Middleware interface {
	Wrap(handlerFunc http.HandlerFunc) http.HandlerFunc
}

type JsonContentTypeMiddleware struct {
}

// Wrap implements the Middleware interface
func (m *JsonContentTypeMiddleware) Wrap(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set(ContentType, ApplicationJsonContentType)
		next(w, r)
	}
}

func NewJsonContentTypeMiddleware() Middleware {
	return &JsonContentTypeMiddleware{}
}
