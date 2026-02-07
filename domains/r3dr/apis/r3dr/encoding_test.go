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
