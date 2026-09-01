// AWS Signature Version 4 for exactly one operation: a header-signed S3
// PUT. Hand-rolled on the usual dep-light reasoning — the SDK is ~20
// modules for what is four HMACs and some canonicalization, and this is
// the repo's first AWS call, so the SDK tree would be bought for one
// request shape. The derivation is pinned against
// AWS's published worked example in sigv4_test.go, intermediate values
// included, so any drift names the stage that drifted.
package s3lite

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"sort"
	"strings"
	"time"
)

// The two date renderings the protocol uses. Shared constants rather than
// repeated literals: the x-amz-date header, the signed x-amz-date value,
// the string to sign and the credential scope must agree byte-for-byte, or
// every request is a SignatureDoesNotMatch.
const (
	amzDateFormat = "20060102T150405Z"
	amzDateOnly   = "20060102"
)

// canonicalURI encodes the request path the way S3's own signer does before
// comparing: every byte percent-encoded except unreserved characters and
// '/'. Go's URL escaping leaves sub-delims like '=' literal, and the
// partition layout (dt=2026-08-31) puts '=' in every key — signing the
// unencoded form is a guaranteed SignatureDoesNotMatch.
func canonicalURI(path string) string {
	var b strings.Builder
	for _, c := range []byte(path) {
		switch {
		case c >= 'A' && c <= 'Z', c >= 'a' && c <= 'z', c >= '0' && c <= '9',
			c == '-', c == '.', c == '_', c == '~', c == '/':
			b.WriteByte(c)
		default:
			fmt.Fprintf(&b, "%%%02X", c)
		}
	}
	return b.String()
}

// canonicalQuery renders query parameters the way S3's signer expects:
// each name and value URI-encoded with the strict rules, pairs sorted by
// encoded name. An empty map is the empty string.
func canonicalQuery(params map[string]string) string {
	encoded := make([]string, 0, len(params))
	for name, value := range params {
		encoded = append(encoded, uriEncode(name)+"="+uriEncode(value))
	}
	sort.Strings(encoded)
	return strings.Join(encoded, "&")
}

// uriEncode is AWS's own encoding: unreserved characters only, uppercase
// hex, space as %20 — Go's url.QueryEscape differs on '+' and '~'.
func uriEncode(value string) string {
	var b strings.Builder
	for _, c := range []byte(value) {
		switch {
		case c >= 'A' && c <= 'Z', c >= 'a' && c <= 'z', c >= '0' && c <= '9',
			c == '-', c == '.', c == '_', c == '~':
			b.WriteByte(c)
		default:
			fmt.Fprintf(&b, "%%%02X", c)
		}
	}
	return b.String()
}

func canonicalRequest(method, path, query, host, payloadHash string, when time.Time, extra map[string]string) (string, string) {
	headers := map[string]string{
		"host":                 host,
		"x-amz-content-sha256": payloadHash,
		"x-amz-date":           when.Format(amzDateFormat),
	}
	for name, value := range extra {
		headers[strings.ToLower(name)] = strings.TrimSpace(value)
	}
	names := make([]string, 0, len(headers))
	for name := range headers {
		names = append(names, name)
	}
	sort.Strings(names)
	signedHeaders := strings.Join(names, ";")

	var b strings.Builder
	b.WriteString(method + "\n")
	b.WriteString(canonicalURI(path) + "\n")
	b.WriteString(query + "\n")
	for _, name := range names {
		b.WriteString(name + ":" + headers[name] + "\n")
	}
	b.WriteString("\n")
	b.WriteString(signedHeaders + "\n")
	b.WriteString(payloadHash)
	return b.String(), signedHeaders
}

func stringToSign(when time.Time, region, canonical string) string {
	hash := sha256.Sum256([]byte(canonical))
	return "AWS4-HMAC-SHA256\n" +
		when.Format(amzDateFormat) + "\n" +
		credentialScope(when, region) + "\n" +
		hex.EncodeToString(hash[:])
}

func credentialScope(when time.Time, region string) string {
	return when.Format(amzDateOnly) + "/" + region + "/s3/aws4_request"
}

func hmacSHA256(key, data []byte) []byte {
	mac := hmac.New(sha256.New, key)
	mac.Write(data)
	return mac.Sum(nil)
}

func signature(secretKey string, when time.Time, region, toSign string) string {
	key := hmacSHA256([]byte("AWS4"+secretKey), []byte(when.Format(amzDateOnly)))
	key = hmacSHA256(key, []byte(region))
	key = hmacSHA256(key, []byte("s3"))
	key = hmacSHA256(key, []byte("aws4_request"))
	return hex.EncodeToString(hmacSHA256(key, []byte(toSign)))
}

func authorizationHeader(accessKey, secretKey string, when time.Time, region,
	method, path, query, host, payloadHash string, extra map[string]string) string {
	canonical, signedHeaders := canonicalRequest(method, path, query, host, payloadHash, when, extra)
	sig := signature(secretKey, when, region, stringToSign(when, region, canonical))
	return "AWS4-HMAC-SHA256 " +
		"Credential=" + accessKey + "/" + credentialScope(when, region) + "," +
		"SignedHeaders=" + signedHeaders + "," +
		"Signature=" + sig
}
