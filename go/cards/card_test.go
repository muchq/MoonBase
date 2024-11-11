package cards

import (
	"github.com/stretchr/testify/assert"
	"testing"
)

func TestDeckHas52Cards(t *testing.T) {
	deck := NewDeck()
	assert.Equal(t, 52, len(deck.Cards), "deck should have 52 cards")
}

func TestShuffle(t *testing.T) {
	deck1 := NewDeck()
	deck2 := NewDeck()

	assert.Equal(t, deck1.Cards, deck2.Cards)

	deck1.Shuffle()
	assert.NotEqual(t, deck1.Cards, deck2.Cards)
}
