package s3lite

import (
	"strings"
	"testing"
	"time"
)

// AWS's published GET-object worked example (Amazon S3 API reference,
// "Signature Calculations for the Authorization Header"): the demo
// credentials, a fixed clock, and three published intermediate values. If
// any stage of the derivation is wrong — canonical form, string to sign,
// key derivation, final MAC — a different published constant pins which one.
const (
	exampleAccessKey = "AKIAIOSFODNN7EXAMPLE"
	exampleSecretKey = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY"
)

var exampleTime = time.Date(2013, 5, 24, 0, 0, 0, 0, time.UTC)

func exampleCanonicalRequest() string {
	return "GET\n" +
		"/test.txt\n" +
		"\n" +
		"host:examplebucket.s3.amazonaws.com\n" +
		"range:bytes=0-9\n" +
		"x-amz-content-sha256:" + emptyPayloadHash + "\n" +
		"x-amz-date:20130524T000000Z\n" +
		"\n" +
		"host;range;x-amz-content-sha256;x-amz-date\n" +
		emptyPayloadHash
}

func TestStringToSignMatchesThePublishedExample(t *testing.T) {
	got := stringToSign(exampleTime, "us-east-1", exampleCanonicalRequest())
	want := "AWS4-HMAC-SHA256\n" +
		"20130524T000000Z\n" +
		"20130524/us-east-1/s3/aws4_request\n" +
		"7344ae5b7ee6c3e7e6b0fe0640412a37625d1fbfff95c48bbb2dc43964946972"
	if got != want {
		t.Errorf("string to sign diverges from the published example:\ngot:\n%s\nwant:\n%s", got, want)
	}
}

func TestSignatureMatchesThePublishedExample(t *testing.T) {
	sts := stringToSign(exampleTime, "us-east-1", exampleCanonicalRequest())
	got := signature(exampleSecretKey, exampleTime, "us-east-1", sts)
	want := "f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41"
	if got != want {
		t.Errorf("signature diverges from the published example: got %s want %s", got, want)
	}
}

// The partition layout puts '=' in object keys (dt=2026-08-31). S3 derives
// its server-side canonical request by URI-encoding the raw path with its
// own rules — '=' becomes %3D — so the client-side canonical form must
// encode it identically or every upload under a partitioned key is a
// SignatureDoesNotMatch. '/' is the one character that stays literal.
func TestCanonicalURIEncodesEqualsButNotSlashes(t *testing.T) {
	got := canonicalURI("/bucket/logs/source=caddy/dt=2026-08-31/access.log.gz")
	want := "/bucket/logs/source%3Dcaddy/dt%3D2026-08-31/access.log.gz"
	if got != want {
		t.Errorf("canonicalURI = %s, want %s", got, want)
	}
}

func TestAuthorizationHeaderCarriesScopeSignedHeadersAndSignature(t *testing.T) {
	header := authorizationHeader(
		exampleAccessKey, exampleSecretKey, exampleTime, "us-east-1",
		"GET", "/test.txt", "", "examplebucket.s3.amazonaws.com", emptyPayloadHash,
		map[string]string{"range": "bytes=0-9"})

	want := "AWS4-HMAC-SHA256 " +
		"Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request," +
		"SignedHeaders=host;range;x-amz-content-sha256;x-amz-date," +
		"Signature=f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41"
	if header != want {
		t.Errorf("authorization header diverges from the published example:\ngot:  %s\nwant: %s", header, want)
	}
}

// The golden vectors above are GETs, so nothing there stops the method line
// from being hardcoded — a mutation under which every production PUT is a
// SignatureDoesNotMatch while the whole suite stays green. Pinned directly:
// the canonical request opens with the method it was given, and an extra
// header lands lowercased, trimmed, and in sorted order.
func TestCanonicalRequestCarriesTheMethodAndExtraHeaders(t *testing.T) {
	canonical, signedHeaders := canonicalRequest(
		"PUT", "/k", "", "bucket.s3.us-east-1.amazonaws.com", emptyPayloadHash,
		exampleTime, map[string]string{"X-Amz-Storage-Class": " REDUCED_REDUNDANCY "})

	if !strings.HasPrefix(canonical, "PUT\n") {
		t.Errorf("canonical request does not open with the request method:\n%s", canonical)
	}
	if want := "host;x-amz-content-sha256;x-amz-date;x-amz-storage-class"; signedHeaders != want {
		t.Errorf("signed headers = %s, want %s", signedHeaders, want)
	}
	if !strings.Contains(canonical, "x-amz-storage-class:REDUCED_REDUNDANCY\n") {
		t.Errorf("extra header is not lowercased and trimmed in the canonical form:\n%s", canonical)
	}
}

// Query parameters are sorted by encoded name and use AWS's strict
// encoding — Go's QueryEscape would emit '+' for space and escape '~',
// both SignatureDoesNotMatch on the wire.
func TestCanonicalQuerySortsAndStrictlyEncodes(t *testing.T) {
	got := canonicalQuery(map[string]string{
		"prefix":      "logs/source=caddy/",
		"list-type":   "2",
		"start-after": "logs/a b~c",
	})
	want := "list-type=2&prefix=logs%2Fsource%3Dcaddy%2F&start-after=logs%2Fa%20b~c"
	if got != want {
		t.Errorf("canonicalQuery = %s, want %s", got, want)
	}
}
