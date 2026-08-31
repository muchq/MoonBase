package log_shipper

import (
	"bytes"
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

func (s *S3) Put(key string, body []byte) error {
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

	payloadHash := sha256.Sum256(body)
	hashHex := hex.EncodeToString(payloadHash[:])
	when := s.Now().UTC()

	req, err := http.NewRequest(http.MethodPut, requestURL, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Set("x-amz-date", when.Format("20060102T150405Z"))
	req.Header.Set("x-amz-content-sha256", hashHex)
	req.Header.Set("Authorization", authorizationHeader(
		s.Creds.AccessKeyID, s.Creds.SecretAccessKey, when, s.Region,
		http.MethodPut, path, host, hashHex, nil))

	resp, err := s.Client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		// S3's reason rides in the XML body; without it a 403 is just "403"
		// and whoever reads the log is left guessing between clock skew, a
		// bad key, and a signing bug.
		reason, _ := io.ReadAll(io.LimitReader(resp.Body, 2048))
		return fmt.Errorf("s3 put %s: HTTP %d: %s", key, resp.StatusCode, reason)
	}
	return nil
}
