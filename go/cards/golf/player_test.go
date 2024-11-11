package golf

import (
	"github.com/muchq/moonbase/go/cards"
	"github.com/stretchr/testify/assert"
	"testing"
)

func TestPlayer_ScoreZero(t *testing.T) {
	player := Player{
		Id:       "123",
		Username: "andy",
		Cards: map[Position]cards.Card{
			TopLeft:     {Suit: "C", Value: "2"},
			TopRight:    {Suit: "D", Value: "2"},
			BottomLeft:  {Suit: "H", Value: "4"},
			BottomRight: {Suit: "S", Value: "4"},
		},
	}

	assert.Equal(t, 0, player.Score(), "pairs cancel")
}

func TestPlayer_ScoreNonZero(t *testing.T) {
	player := Player{
		Id:       "123",
		Username: "andy",
		Cards: map[Position]cards.Card{
			TopLeft:     {Suit: "C", Value: "2"},
			TopRight:    {Suit: "D", Value: "3"},
			BottomLeft:  {Suit: "H", Value: "Q"},
			BottomRight: {Suit: "S", Value: "5"},
		},
	}

	assert.Equal(t, 20, player.Score(), "non-matching cards add up")
}

func TestPlayer_ScoreJack(t *testing.T) {
	player := Player{
		Id:       "123",
		Username: "andy",
		Cards: map[Position]cards.Card{
			TopLeft:     {Suit: "C", Value: "2"},
			TopRight:    {Suit: "D", Value: "2"},
			BottomLeft:  {Suit: "H", Value: "J"},
			BottomRight: {Suit: "S", Value: "A"},
		},
	}

	assert.Equal(t, 1, player.Score(), "jacks are zero")
}
