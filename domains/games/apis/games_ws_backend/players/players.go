package players

import (
	"crypto/rand"
	"fmt"
	"time"
)

// PlayerIDGenerator defines the interface for generating player IDs
type PlayerIDGenerator interface {
	GenerateID() string
}

// DeterministicIDGenerator generates predictable IDs for testing
type DeterministicIDGenerator struct {
	counter int
}

// GenerateID creates a deterministic player ID for testing
func (g *DeterministicIDGenerator) GenerateID() string {
	g.counter++
	return fmt.Sprintf("player-%d", g.counter)
}

// NewDeterministicIDGenerator creates a new deterministic ID generator
func NewDeterministicIDGenerator() *DeterministicIDGenerator {
	return &DeterministicIDGenerator{counter: 0}
}

// WhimsicalIDGenerator generates fun, memorable player IDs
type WhimsicalIDGenerator struct{}

var (
	whimsicalAdjectives = []string{
		"bouncy", "giggly", "sparkly", "fuzzy", "wiggly",
		"snuggly", "dreamy", "bubbly", "twinkly", "jolly",
		"quirky", "peppy", "zesty", "frisky", "silly",
		"perky", "cheeky", "zippy", "groovy", "jazzy",
	}

	whimsicalColors = []string{
		"lavender", "periwinkle", "coral", "mint", "peach",
		"turquoise", "magenta", "cerulean", "lilac", "salmon",
		"chartreuse", "crimson", "cobalt", "amber", "jade",
		"fuchsia", "indigo", "teal", "mauve", "vermillion",
	}

	cuteAustralianAnimals = []string{
		"koala", "kangaroo", "wombat", "quokka", "platypus",
		"echidna", "wallaby", "bilby", "numbat", "possum",
		"kookaburra", "cockatoo", "lorikeet", "galah", "budgie",
		"dingo", "bandicoot", "pademelon", "potoroo", "glider",
	}
)

// GenerateID creates a whimsical player ID in format: {adj}-{color}-{animal}-{4char}
func (g *WhimsicalIDGenerator) GenerateID() string {
	// Generate random indices for each component
	bytes := make([]byte, 7) // 3 for array indices + 4 for alphanumeric slug
	if _, err := rand.Read(bytes); err != nil {
		// Fallback to time-based ID if crypto/rand fails
		return fmt.Sprintf("player-%d", time.Now().UnixNano()%1000000)
	}

	// Select random words from each list
	adjective := whimsicalAdjectives[int(bytes[0])%len(whimsicalAdjectives)]
	color := whimsicalColors[int(bytes[1])%len(whimsicalColors)]
	animal := cuteAustralianAnimals[int(bytes[2])%len(cuteAustralianAnimals)]

	// Generate 4-character alphanumeric slug
	const charset = "abcdefghijklmnopqrstuvwxyz0123456789"
	slug := make([]byte, 4)
	for i := 0; i < 4; i++ {
		slug[i] = charset[bytes[3+i]%byte(len(charset))]
	}

	return fmt.Sprintf("%s-%s-%s-%s", adjective, color, animal, string(slug))
}
