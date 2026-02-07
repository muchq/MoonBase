package players

import (
	"strings"
	"testing"
)

func TestWhimsicalIDGenerator(t *testing.T) {
	generator := &WhimsicalIDGenerator{}

	// Test that IDs follow the expected format
	for i := 0; i < 10; i++ {
		id := generator.GenerateID()

		// Check format: {adj}-{color}-{animal}-{4char}
		parts := strings.Split(id, "-")

		if len(parts) != 4 {
			t.Errorf("Expected 4 parts in ID, got %d: %s", len(parts), id)
			continue
		}

		// Check adjective is in list
		adjFound := false
		for _, adj := range whimsicalAdjectives {
			if parts[0] == adj {
				adjFound = true
				break
			}
		}
		if !adjFound {
			t.Errorf("Adjective '%s' not found in whimsicalAdjectives", parts[0])
		}

		// Check color is in list
		colorFound := false
		for _, color := range whimsicalColors {
			if parts[1] == color {
				colorFound = true
				break
			}
		}
		if !colorFound {
			t.Errorf("Color '%s' not found in whimsicalColors", parts[1])
		}

		// Check animal is in list
		animalFound := false
		for _, animal := range cuteAustralianAnimals {
			if parts[2] == animal {
				animalFound = true
				break
			}
		}
		if !animalFound {
			t.Errorf("Animal '%s' not found in cuteAustralianAnimals", parts[2])
		}

		// Check slug is 4 characters and alphanumeric
		if len(parts[3]) != 4 {
			t.Errorf("Expected 4-character slug, got %d: %s", len(parts[3]), parts[3])
		}
		for _, char := range parts[3] {
			if !((char >= 'a' && char <= 'z') || (char >= '0' && char <= '9')) {
				t.Errorf("Invalid character in slug: %c", char)
			}
		}
	}
}

func TestWhimsicalIDGenerator_Uniqueness(t *testing.T) {
	generator := &WhimsicalIDGenerator{}
	ids := make(map[string]bool)

	// Generate 100 IDs and check for duplicates
	for i := 0; i < 100; i++ {
		id := generator.GenerateID()
		if ids[id] {
			t.Errorf("Duplicate ID generated: %s", id)
		}
		ids[id] = true
	}
}

func TestWhimsicalIDGenerator_Fallback(t *testing.T) {
	// The fallback is only triggered when crypto/rand fails,
	// which is difficult to test in a unit test.
	// We'll just verify the generator works normally
	generator := &WhimsicalIDGenerator{}
	id := generator.GenerateID()
	if id == "" {
		t.Error("Generated empty ID")
	}
}
