package golf

import (
	"github.com/muchq/moonbase/go/cards"
	"github.com/stretchr/testify/assert"
	"testing"
)

func TestGameState_IsOver(t *testing.T) {
	p0 := player0()
	p1 := player1()

	emptyDrawPile := cards.Deck{}
	nonEmptyDrawPile := cards.Deck{Cards: []cards.Card{{Suit: "C", Value: "A"}}}
	emptyDiscardPile := cards.Deck{}
	players := []Player{p0, p1}
	whoseTurn := 0
	whoKnocked := -1

	g1 := newGame(emptyDrawPile, emptyDiscardPile, players, whoseTurn, whoKnocked)
	assert.True(t, g1.IsOver(), "game is over when draw pile is empty")

	g2 := newGame(nonEmptyDrawPile, emptyDiscardPile, players, 0, -1)
	assert.False(t, g2.IsOver(), "no one knocked and there's still a card on the draw pile")

	g3 := newGame(nonEmptyDrawPile, emptyDiscardPile, players, 1, 1)
	assert.True(t, g3.IsOver(), "player 1 knocked and it's their turn again")
}

func TestGameState_Winners(t *testing.T) {
	p0 := player0()
	p1 := player1()

	emptyDrawPile := cards.Deck{}
	nonEmptyDrawPile := cards.Deck{Cards: []cards.Card{{Suit: "C", Value: "A"}}}
	emptyDiscardPile := cards.Deck{}
	players := []Player{p0, p1}
	whoseTurn1 := 0
	whoKnocked1 := -1

	g1 := newGame(emptyDrawPile, emptyDiscardPile, players, whoseTurn1, whoKnocked1)

	expectedWinners1 := []int{0, 1}
	assert.True(t, g1.IsOver(), "game is over when draw pile is empty")
	assert.Equal(t, expectedWinners1, g1.Winners(), "scores are equal and no one knocked")

	whoseTurn2 := 1
	whoKnocked2 := 1
	g2 := newGame(nonEmptyDrawPile, emptyDiscardPile, players, whoseTurn2, whoKnocked2)

	expectedWinners2 := []int{1}
	assert.True(t, g2.IsOver(), "player 1 knocked and it's their turn again")
	assert.Equal(t, expectedWinners2, g2.Winners(), "tie goes to the runner")
}

func TestGameState_SwapForDrawPile(t *testing.T) {
	p0 := player0()
	p1 := player1()

	drawPile := cards.Deck{Cards: []cards.Card{
		{Suit: "D", Value: "J"},
		{Suit: "C", Value: "A"},
	}}
	discardPile := cards.Deck{}
	players := []Player{p0, p1}
	whoseTurn := 1
	whoKnocked := -1

	initialState := newGame(drawPile, discardPile, players, whoseTurn, whoKnocked)
	// should swap p1's top left card for Ace of Clubs
	updatedState, err := initialState.SwapForDrawPile(1, TopLeft)

	assert.Nil(t, err, "swap should be allowed")
	assert.False(t, updatedState.IsOver(), "game should not be over yet")

	// check draw pile
	expectedDrawPile := cards.Deck{Cards: []cards.Card{
		{Suit: "D", Value: "J"},
	}}
	assert.Equal(t, expectedDrawPile, updatedState.DrawPile)

	// check discard pile
	expectedDiscardPile := cards.Deck{Cards: []cards.Card{
		{Suit: "C", Value: "3"},
	}}
	assert.Equal(t, expectedDiscardPile, updatedState.DiscardPile)

	// check players
	assert.Equal(t, p0, updatedState.Players[0], "player 0 should not be updated")

	expectedP1 := Player{
		Id:       "234",
		Username: "Mercy",
		Cards: map[Position]cards.Card{
			TopLeft:     {Suit: "C", Value: "A"},
			TopRight:    {Suit: "D", Value: "3"},
			BottomLeft:  {Suit: "H", Value: "3"},
			BottomRight: {Suit: "S", Value: "3"},
		},
	}
	assert.Equal(t, expectedP1, updatedState.Players[1])

	// check whose turn
	assert.Equal(t, 0, updatedState.WhoseTurn, "should be player 0's turn now")

	// check who knocked
	assert.Equal(t, -1, updatedState.WhoKnocked, "no one knocked")

	// check game id
	assert.Equal(t, "123", updatedState.Id, "game id should not be changed")
}

func TestGameState_SwapForDrawPile_FailsWhenGameIsOver(t *testing.T) {
	p0 := player0()
	p1 := player1()

	drawPile := cards.Deck{Cards: []cards.Card{
		{Suit: "D", Value: "J"},
		{Suit: "C", Value: "A"},
	}}
	discardPile := cards.Deck{}
	players := []Player{p0, p1}
	whoseTurn := 1
	whoKnocked := 1

	initialState := newGame(drawPile, discardPile, players, whoseTurn, whoKnocked)
	updatedState, err := initialState.SwapForDrawPile(1, TopLeft)

	assert.Nil(t, updatedState, "new state should not be generated")
	assert.EqualError(t, err, "game is over")
}

func TestGameState_SwapForDrawPile_FailsWhenNotYourTurn(t *testing.T) {
	p0 := player0()
	p1 := player1()

	drawPile := cards.Deck{Cards: []cards.Card{
		{Suit: "D", Value: "J"},
		{Suit: "C", Value: "A"},
	}}
	discardPile := cards.Deck{}
	players := []Player{p0, p1}
	whoseTurn := 0
	whoKnocked := -1

	initialState := newGame(drawPile, discardPile, players, whoseTurn, whoKnocked)
	updatedState, err := initialState.SwapForDrawPile(1, TopLeft)

	assert.Nil(t, updatedState, "new state should not be generated")
	assert.EqualError(t, err, "not your turn")
}

func TestGameState_PeekAtDrawPile(t *testing.T) {
	p0 := player0()
	p1 := player1()

	drawPile := cards.Deck{Cards: []cards.Card{
		{Suit: "D", Value: "J"},
		{Suit: "C", Value: "A"},
	}}
	discardPile := cards.Deck{}
	players := []Player{p0, p1}
	whoseTurn := 0
	whoKnocked := -1

	initialState := newGame(drawPile, discardPile, players, whoseTurn, whoKnocked)
	updatedState, err := initialState.PeekAtDrawPile(0)

	expectedState := &GameState{
		Id:               "123",
		Version:          "foo",
		Players:          players,
		PeekedAtDrawPile: true,
		WhoseTurn:        0,
		WhoKnocked:       -1,
		DrawPile:         drawPile,
		DiscardPile:      discardPile,
	}

	assert.Nil(t, err, "err be nil")
	assert.Equal(t, expectedState, updatedState, "PeekedAtDrawPile should be true")
}

func TestGameState_PeekAtDrawPile_FailsWhenNotYourTurn(t *testing.T) {
	p0 := player0()
	p1 := player1()

	drawPile := cards.Deck{Cards: []cards.Card{
		{Suit: "D", Value: "J"},
		{Suit: "C", Value: "A"},
	}}
	discardPile := cards.Deck{}
	players := []Player{p0, p1}
	whoseTurn := 0
	whoKnocked := -1

	initialState := newGame(drawPile, discardPile, players, whoseTurn, whoKnocked)
	updatedState, err := initialState.PeekAtDrawPile(1)

	assert.Nil(t, updatedState, "new state should not be generated")
	assert.EqualError(t, err, "not your turn")
}

func player0() Player {
	return Player{
		Id:       "123",
		Username: "Andy",
		Cards: map[Position]cards.Card{
			TopLeft:     {Suit: "C", Value: "2"},
			TopRight:    {Suit: "D", Value: "2"},
			BottomLeft:  {Suit: "H", Value: "2"},
			BottomRight: {Suit: "S", Value: "2"},
		},
	}
}

func player1() Player {
	return Player{
		Id:       "234",
		Username: "Mercy",
		Cards: map[Position]cards.Card{
			TopLeft:     {Suit: "C", Value: "3"},
			TopRight:    {Suit: "D", Value: "3"},
			BottomLeft:  {Suit: "H", Value: "3"},
			BottomRight: {Suit: "S", Value: "3"},
		},
	}
}

func newGame(drawPile cards.Deck, discardPile cards.Deck, players []Player, whoseTurn int, whoKnocked int) *GameState {
	return &GameState{
		Id:               "123",
		Version:          "foo",
		Players:          players,
		PeekedAtDrawPile: false,
		WhoseTurn:        whoseTurn,
		WhoKnocked:       whoKnocked,
		DrawPile:         drawPile,
		DiscardPile:      discardPile,
	}
}
