package log_shipper

import (
	"bytes"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

// A deliberately non-UTC clock whose UTC rendering is 20260831T100000Z: the
// signer must convert, and a UTC-only fixture would let a dropped .UTC()
// pass here and 403 on any host with a local timezone.
func signingClock() time.Time {
	return time.Date(2026, 8, 31, 12, 0, 0, 0, time.FixedZone("UTC+2", 2*3600))
}

func testS3(t *testing.T, handler http.HandlerFunc) *S3 {
	t.Helper()
	server := httptest.NewServer(handler)
	t.Cleanup(server.Close)
	return &S3{
		Bucket:   "stats-bucket",
		Region:   "us-east-1",
		Creds:    Credentials{AccessKeyID: "AKID", SecretAccessKey: "secret"},
		Client:   server.Client(),
		Endpoint: server.URL,
		Now:      signingClock,
	}
}

func put(t *testing.T, s3 *S3, key, body string) error {
	t.Helper()
	return s3.Put(key, bytes.NewReader([]byte(body)), int64(len(body)))
}

func TestPutSendsASignedRequestWithTheBody(t *testing.T) {
	var got *http.Request
	var body []byte
	s3 := testS3(t, func(w http.ResponseWriter, r *http.Request) {
		got = r
		body, _ = io.ReadAll(r.Body)
	})

	if err := put(t, s3, "logs/source=caddy/dt=2026-08-31/x.log.gz", "payload"); err != nil {
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
	if got.Header.Get("x-amz-date") != "20260831T100000Z" {
		t.Errorf("x-amz-date = %s, want the UTC rendering of the non-UTC clock",
			got.Header.Get("x-amz-date"))
	}
	// The signed payload hash must be of the actual body, not UNSIGNED-PAYLOAD:
	// sha256("payload").
	if want := "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5"; got.Header.Get("x-amz-content-sha256") != want {
		t.Errorf("x-amz-content-sha256 = %s, want the body's hash", got.Header.Get("x-amz-content-sha256"))
	}
}

// The wire request and the signature must describe the same request. The
// server recomputes the Authorization header from what it actually received
// — method, path, host, payload hash — the way S3 does, and requires exact
// equality. Any divergence between what Put sends and what it signs (the
// path is the classic one) is a production SignatureDoesNotMatch that the
// substring assertions above cannot see.
func TestTheSignatureCoversTheRequestActuallySent(t *testing.T) {
	var got *http.Request
	s3 := testS3(t, func(w http.ResponseWriter, r *http.Request) { got = r })

	key := "logs/source=caddy/dt=2026-08-31/access-x.log.gz"
	if err := put(t, s3, key, "payload"); err != nil {
		t.Fatal(err)
	}

	recomputed := authorizationHeader(
		"AKID", "secret", signingClock().UTC(), "us-east-1",
		got.Method, got.URL.Path, got.Host, got.Header.Get("x-amz-content-sha256"), nil)
	if auth := got.Header.Get("Authorization"); auth != recomputed {
		t.Errorf("Authorization does not cover the request as sent:\nsent:       %s\nrecomputed: %s",
			auth, recomputed)
	}
}

// With no Endpoint override — production — the URL is virtual-hosted and
// the signature covers that host and the bare key path. This is the one
// branch the httptest-based tests never enter.
func TestProductionUsesTheVirtualHostedURLAndSignsIt(t *testing.T) {
	var got *http.Request
	s3 := &S3{
		Bucket: "stats-bucket",
		Region: "us-east-1",
		Creds:  Credentials{AccessKeyID: "AKID", SecretAccessKey: "secret"},
		Client: &http.Client{Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
			got = r
			return &http.Response{StatusCode: http.StatusOK, Body: http.NoBody}, nil
		})},
		Now: signingClock,
	}

	key := "logs/source=caddy/dt=2026-08-31/x.log.gz"
	if err := put(t, s3, key, "payload"); err != nil {
		t.Fatal(err)
	}

	if want := "stats-bucket.s3.us-east-1.amazonaws.com"; got.URL.Host != want {
		t.Errorf("host = %s, want %s", got.URL.Host, want)
	}
	if want := "/" + key; got.URL.Path != want {
		t.Errorf("path = %s, want %s", got.URL.Path, want)
	}
	recomputed := authorizationHeader(
		"AKID", "secret", signingClock().UTC(), "us-east-1",
		http.MethodPut, got.URL.Path, got.URL.Host, got.Header.Get("x-amz-content-sha256"), nil)
	if auth := got.Header.Get("Authorization"); auth != recomputed {
		t.Errorf("Authorization does not cover the virtual-hosted request:\nsent:       %s\nrecomputed: %s",
			auth, recomputed)
	}
}

type roundTripFunc func(*http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

func TestPutReportsANon200AsAnErrorWithTheResponseBody(t *testing.T) {
	s3 := testS3(t, func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusForbidden)
		w.Write([]byte("<Error>SignatureDoesNotMatch</Error>"))
	})

	err := put(t, s3, "k", "x")

	if err == nil {
		t.Fatal("a 403 must not read as a successful upload — the caller deletes on success")
	}
	for _, want := range []string{"403", "SignatureDoesNotMatch"} {
		if !strings.Contains(err.Error(), want) {
			t.Errorf("error %q lacks %q; whoever reads the log needs S3's reason", err, want)
		}
	}
}

// A region redirect must surface as itself. Followed, Go's client would
// replay against a host the signature does not cover and the resulting 403
// reads as a credential problem; the 307's own status and Location are the
// diagnosis.
func TestARegionRedirectIsReportedWithItsLocationNotFollowed(t *testing.T) {
	requests := 0
	s3 := testS3(t, func(w http.ResponseWriter, r *http.Request) {
		requests++
		w.Header().Set("Location", "https://stats-bucket.s3.eu-west-1.amazonaws.com/k")
		w.WriteHeader(http.StatusTemporaryRedirect)
	})

	err := put(t, s3, "k", "x")

	if requests != 1 {
		t.Errorf("the redirect was followed (%d requests); the replay carries a signature "+
			"for the wrong host", requests)
	}
	if err == nil {
		t.Fatal("a redirect must not read as a successful upload")
	}
	for _, want := range []string{"307", "eu-west-1"} {
		if !strings.Contains(err.Error(), want) {
			t.Errorf("error %q lacks %q; the operator needs the region, not a bare 403", err, want)
		}
	}
}
