package log_shipper

import (
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func testS3(t *testing.T, handler http.HandlerFunc) (*S3, *httptest.Server) {
	t.Helper()
	server := httptest.NewServer(handler)
	t.Cleanup(server.Close)
	return &S3{
		Bucket:   "stats-bucket",
		Region:   "us-east-1",
		Creds:    Credentials{AccessKeyID: "AKID", SecretAccessKey: "secret"},
		Client:   server.Client(),
		Endpoint: server.URL,
		Now:      func() time.Time { return time.Date(2026, 8, 31, 10, 0, 0, 0, time.UTC) },
	}, server
}

func TestPutSendsASignedRequestWithTheBody(t *testing.T) {
	var got *http.Request
	var body []byte
	s3, _ := testS3(t, func(w http.ResponseWriter, r *http.Request) {
		got = r
		body, _ = io.ReadAll(r.Body)
	})

	if err := s3.Put("logs/source=caddy/dt=2026-08-31/x.log.gz", []byte("payload")); err != nil {
		t.Fatal(err)
	}

	if got.Method != http.MethodPut {
		t.Errorf("method = %s, want PUT", got.Method)
	}
	if want := "/stats-bucket/logs/source=caddy/dt=2026-08-31/x.log.gz"; got.URL.Path != want {
		t.Errorf("path = %s, want %s", got.URL.Path, want)
	}
	if string(body) != "payload" {
		t.Errorf("body = %q, want the object bytes", body)
	}
	auth := got.Header.Get("Authorization")
	for _, part := range []string{
		"AWS4-HMAC-SHA256 ",
		"Credential=AKID/20260831/us-east-1/s3/aws4_request",
		"SignedHeaders=host;x-amz-content-sha256;x-amz-date",
		"Signature=",
	} {
		if !strings.Contains(auth, part) {
			t.Errorf("Authorization %q lacks %q", auth, part)
		}
	}
	if got.Header.Get("x-amz-date") != "20260831T100000Z" {
		t.Errorf("x-amz-date = %s", got.Header.Get("x-amz-date"))
	}
	// The signed payload hash must be of the actual body, not UNSIGNED-PAYLOAD:
	// sha256("payload").
	if want := "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5"; got.Header.Get("x-amz-content-sha256") != want {
		t.Errorf("x-amz-content-sha256 = %s, want the body's hash", got.Header.Get("x-amz-content-sha256"))
	}
}

func TestPutReportsANon200AsAnErrorWithTheResponseBody(t *testing.T) {
	s3, _ := testS3(t, func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusForbidden)
		w.Write([]byte("<Error>SignatureDoesNotMatch</Error>"))
	})

	err := s3.Put("k", []byte("x"))

	if err == nil {
		t.Fatal("a 403 must not read as a successful upload — the caller deletes on success")
	}
	for _, want := range []string{"403", "SignatureDoesNotMatch"} {
		if !strings.Contains(err.Error(), want) {
			t.Errorf("error %q lacks %q; whoever reads the log needs S3's reason", err, want)
		}
	}
}
