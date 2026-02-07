package mucks

import "github.com/google/uuid"

// Problem represents an error response based on RFC 7807 (Problem Details for HTTP APIs).
type Problem struct {
	StatusCode int    `json:"status"`
	ErrorCode  int    `json:"errorCode"`
	Message    string `json:"message"`
	Detail     string `json:"detail"`
	Instance   string `json:"instance"`
}

func NewBadRequest(detail string) Problem {
	return Problem{
		StatusCode: 400,
		ErrorCode:  400,
		Message:    "Bad Request",
		Detail:     detail,
		Instance:   uuid.NewString(),
	}
}

func NewServerError(code int) Problem {
	return Problem{
		StatusCode: 500,
		ErrorCode:  code,
		Message:    "Internal Error",
		Detail:     "Internal Error",
		Instance:   uuid.NewString(),
	}
}

func NewNotFound() Problem {
	return Problem{
		StatusCode: 404,
		ErrorCode:  404,
		Message:    "Not Found",
		Detail:     "Not Found",
		Instance:   uuid.NewString(),
	}
}
