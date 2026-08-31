package log_shipper

import (
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
	// SHA256 of the empty payload.
	emptyPayloadHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
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
		"GET", "/test.txt", "examplebucket.s3.amazonaws.com", emptyPayloadHash,
		map[string]string{"range": "bytes=0-9"})

	want := "AWS4-HMAC-SHA256 " +
		"Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request," +
		"SignedHeaders=host;range;x-amz-content-sha256;x-amz-date," +
		"Signature=f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41"
	if header != want {
		t.Errorf("authorization header diverges from the published example:\ngot:  %s\nwant: %s", header, want)
	}
}
