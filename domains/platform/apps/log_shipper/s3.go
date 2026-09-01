package log_shipper

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"time"
)

type Credentials struct {
	AccessKeyID     string
	SecretAccessKey string
}

// S3 is a client for exactly one call: a header-signed PutObject.
//
// Endpoint is empty in production, which selects the virtual-hosted AWS URL
// (https://<bucket>.s3.<region>.amazonaws.com/<key>); a test points it at an
// httptest server and the bucket moves into the path. Now is the signing
// clock, injectable for the same reason.
type S3 struct {
	Bucket   string
	Region   string
	Creds    Credentials
	Client   *http.Client
	Endpoint string
	Now      func() time.Time
}

// Put streams body to the bucket. The body is read twice — once for the
// payload hash the signature covers, once for the send — which is the whole
// reason the parameter is a ReadSeeker: a roll is up to a gigabyte and the
// container's memory cap is a fraction of that, so nothing here may buffer
// the object.
func (s *S3) Put(key string, body io.ReadSeeker, size int64) error {
	var requestURL, host, path string
	if s.Endpoint == "" {
		host = s.Bucket + ".s3." + s.Region + ".amazonaws.com"
		path = "/" + key
		requestURL = "https://" + host + path
	} else {
		parsed, err := url.Parse(s.Endpoint)
		if err != nil {
			return err
		}
		host = parsed.Host
		path = "/" + s.Bucket + "/" + key
		requestURL = s.Endpoint + path
	}

	hasher := sha256.New()
	if _, err := io.Copy(hasher, body); err != nil {
		return err
	}
	if _, err := body.Seek(0, io.SeekStart); err != nil {
		return err
	}
	hashHex := hex.EncodeToString(hasher.Sum(nil))
	when := s.Now().UTC()

	req, err := http.NewRequest(http.MethodPut, requestURL, body)
	if err != nil {
		return err
	}
	req.ContentLength = size
	req.Header.Set("x-amz-date", when.Format(amzDateFormat))
	req.Header.Set("x-amz-content-sha256", hashHex)
	req.Header.Set("Authorization", authorizationHeader(
		s.Creds.AccessKeyID, s.Creds.SecretAccessKey, when, s.Region,
		http.MethodPut, path, host, hashHex, nil))

	// Redirects are reported, never followed: Go's client would replay the
	// request against a host the signature does not cover, and the eventual
	// 403 would read as a credential problem instead of the region problem
	// it is. Copied so the injected client is not mutated.
	client := *s.Client
	client.CheckRedirect = func(*http.Request, []*http.Request) error {
		return http.ErrUseLastResponse
	}

	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		// S3's reason rides in the XML body; without it a 403 is just "403"
		// and whoever reads the log is left guessing between clock skew, a
		// bad key, and a signing bug. A redirect's Location names the region
		// mismatch the same way.
		reason, _ := io.ReadAll(io.LimitReader(resp.Body, 2048))
		if location := resp.Header.Get("Location"); location != "" {
			return fmt.Errorf("s3 put %s: HTTP %d redirect to %s: %s",
				key, resp.StatusCode, location, reason)
		}
		return fmt.Errorf("s3 put %s: HTTP %d: %s", key, resp.StatusCode, reason)
	}
	return nil
}
