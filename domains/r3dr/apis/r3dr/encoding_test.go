package main

import (
	"math"
	"testing"
)

func TestEncode(t *testing.T) {
	slug, err := EncodeId(12)
	if err != nil {
		t.Error("12 is a valid id")
	}

	if slug != "DAA" {
		t.Error("slug should be DAA, got", slug)
	}
}

func TestEncodeIdReturnsErrorOnNegativeInput(t *testing.T) {
	_, err := EncodeId(-1)
	if err == nil {
		t.Error("EncodeId should return error on negative input")
	}
}

func TestEncodeIdReturnsShortSlugOnInt8(t *testing.T) {
	slug, err := EncodeId(math.MaxInt8)
	if err != nil {
		t.Error("127 is an allowed value")
	}

	if slug != "fwA" {
		t.Error("MaxInt8 should map to fwA. got", slug)
	}
}

func TestEncodeIdReturnsShortSlugOnInt16(t *testing.T) {
	slug, err := EncodeId(math.MaxInt16)
	if err != nil {
		t.Error("32767 is an allowed value")
	}

	if slug != "_38" {
		t.Error("MaxInt16 should map to _38. got", slug)
	}
}

func TestEncodeIdReturnsShortSlugOnInt32(t *testing.T) {
	slug, err := EncodeId(math.MaxInt32)
	if err != nil {
		t.Error("2147483647 is an allowed value")
	}

	if slug != "____fw" {
		t.Error("MaxInt32 should map to ____fw. got", slug)
	}
}

func TestEncodeIdReturnsShortSlugOnInt64(t *testing.T) {
	slug, err := EncodeId(math.MaxInt64)
	if err != nil {
		t.Error("9223372036854775807 is an allowed value")
	}

	if slug != "_________38" {
		t.Error("MaxInt64 should map to _________38. got", slug)
	}
}

// The width transitions, pinned for the smithy-cpp port (MoonBase#1359).
// IntToBytes switches encoding width at MaxInt16 and MaxInt32, and the slugs
// on either side of each step are what a reimplementation gets wrong: the
// little-endian byte order, the widths themselves, and the RawURL alphabet
// (no padding, - and _ rather than + and /).
//
// Existing rows keep resolving no matter what the port does here — slugs are
// stored, not recomputed — but the id sequence continues across the cutover,
// so an encoder that disagrees starts minting slugs that collide with the
// short_url UNIQUE index. The 3/6/11-char widths are also why no two ranges
// can collide with each other today.
func TestEncodeIdAtWidthTransitions(t *testing.T) {
	cases := []struct {
		id   int64
		slug string
	}{
		{0, "AAA"},
		{1, "AQA"},
		{math.MaxInt16, "_38"},             // last 2-byte id
		{math.MaxInt16 + 1, "AIAAAA"},      // first 4-byte id
		{math.MaxInt32, "____fw"},          // last 4-byte id
		{math.MaxInt32 + 1, "AAAAgAAAAAA"}, // first 8-byte id
	}

	for _, testCase := range cases {
		slug, err := EncodeId(testCase.id)
		if err != nil {
			t.Errorf("EncodeId(%d) returned an error: %v", testCase.id, err)
			continue
		}
		if slug != testCase.slug {
			t.Errorf("EncodeId(%d) = %q, want %q", testCase.id, slug, testCase.slug)
		}
	}
}

// Slug length is a function of the id's width, and nothing else. A port that
// picked fixed 8-byte encoding would still round-trip every id and still be
// wrong: every slug would be 11 chars, and r3dr's whole point is short ones.
func TestSlugWidthsAreThreeSixOrEleven(t *testing.T) {
	for _, id := range []int64{0, 1, math.MaxInt16} {
		assertSlugLen(t, id, 3)
	}
	for _, id := range []int64{math.MaxInt16 + 1, math.MaxInt32} {
		assertSlugLen(t, id, 6)
	}
	for _, id := range []int64{math.MaxInt32 + 1, math.MaxInt64} {
		assertSlugLen(t, id, 11)
	}
}

func assertSlugLen(t *testing.T, id int64, want int) {
	t.Helper()
	slug, err := EncodeId(id)
	if err != nil {
		t.Errorf("EncodeId(%d) returned an error: %v", id, err)
		return
	}
	if len(slug) != want {
		t.Errorf("EncodeId(%d) = %q, want a %d-char slug, got %d", id, slug, want, len(slug))
	}
}
