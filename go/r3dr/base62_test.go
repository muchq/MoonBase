package main

import (
	"math"
	"testing"
)

func TestBase62ReturnsErrorOnNegativeInput(t *testing.T) {
	_, err := ToBase62(-1)
	if err == nil {
		t.Error("Base62 should return error on negative input")
	}
}

func TestBase62ReturnsShortSlugOnInt8(t *testing.T) {
	slug, err := ToBase62(math.MaxInt8)
	if err != nil {
		t.Error("127 is an allowed value")
	}

	if slug != "8so" {
		t.Error("MaxInt8 should map to 8so")
	}
}

func TestBase62ReturnsShortSlugOnInt16(t *testing.T) {
	slug, err := ToBase62(math.MaxInt16)
	if err != nil {
		t.Error("32767 is an allowed value")
	}

	if slug != "h0X" {
		t.Error("MaxInt16 should map to h0X")
	}
}

func TestBase62ReturnsShortSlugOnInt32(t *testing.T) {
	slug, err := ToBase62(math.MaxInt32)
	if err != nil {
		t.Error("2147483647 is an allowed value")
	}

	if slug != "4GFf9Z" {
		t.Error("MaxInt32 should map to 4GFf9Z")
	}
}

func TestBase62ReturnsShortSlugOnInt64(t *testing.T) {
	slug, err := ToBase62(math.MaxInt64)
	if err != nil {
		t.Error("9223372036854775807 is an allowed value")
	}

	if slug != "lYGhA16ahwb" {
		t.Error("MaxInt64 should map to lYGhA16ahwb")
	}
}
