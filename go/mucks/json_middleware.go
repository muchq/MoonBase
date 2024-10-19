package mucks

import "net/http"

type JsonContentTypeMiddleware struct {
}

func (m *JsonContentTypeMiddleware) Wrap(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set(ContentType, ApplicationJsonContentType)
		next(w, r)
	}
}
