package golf

import (
	"errors"
	"github.com/muchq/moonbase/go/cards"
	"slices"
)

type GameState struct {
	Id               string
	Version          string
	Players          []Player
	PeekedAtDrawPile bool
	WhoseTurn        int
	WhoKnocked       int
	DrawPile         cards.Deck
	DiscardPile      cards.Deck
}

func (g *GameState) IsOver() bool {
	return g.DrawPile.IsEmpty() || g.WhoseTurn == g.WhoKnocked
}

func (g *GameState) AllPlayersPresent() bool {
	for _, player := range g.Players {
		if player.Id == "" {
			return false
		}
	}
	return true
}

func (g *GameState) Winners() []int {
	winningPlayerIndexes := make([]int, 0)
	minScore := 40 // max score is 9 10 Q K == 39
	for index, player := range g.Players {
		pScore := player.Score()
		if pScore < minScore {
			minScore = pScore
			winningPlayerIndexes = nil
		}
		if pScore == minScore {
			winningPlayerIndexes = append(winningPlayerIndexes, index)
		}
	}

	// Tie goes to the runner
	if slices.Contains(winningPlayerIndexes, g.WhoKnocked) {
		winningPlayerIndexes = nil
		winningPlayerIndexes = append(winningPlayerIndexes, g.WhoKnocked)
	}

	return winningPlayerIndexes
}

func (g *GameState) PeekAtDrawPile(player int) (*GameState, error) {
	if g.IsOver() {
		return nil, errors.New("game is over")
	}

	if !g.AllPlayersPresent() {
		return nil, errors.New("not all players have joined")
	}

	if g.WhoseTurn != player {
		return nil, errors.New("not your turn")
	}

	if g.PeekedAtDrawPile {
		return nil, errors.New("you can only peek once per turn")
	}

	var gameState = *g
	gameState.PeekedAtDrawPile = true

	return &gameState, nil
}

func (g *GameState) SwapDrawForDiscard(player int) (*GameState, error) {
	if g.IsOver() {
		return nil, errors.New("game is over")
	}
	if !g.AllPlayersPresent() {
		return nil, errors.New("not all players have joined")
	}
	if g.WhoseTurn != player {
		return nil, errors.New("not your turn")
	}

	// update draw pile
	var updatedDrawPile = g.DrawPile
	updatedDrawPtr := &updatedDrawPile
	toSwapIntoDiscard := updatedDrawPtr.RemoveLast()

	// update discard pile
	var updatedDiscardPile = g.DiscardPile
	updatedDiscardPtr := &updatedDiscardPile
	updatedDiscardPtr.AddLast(toSwapIntoDiscard)

	// update whose turn it is
	newWhoseTurn := (g.WhoseTurn + 1) % len(g.Players)

	return &GameState{
		DrawPile:         updatedDrawPile,
		DiscardPile:      updatedDiscardPile,
		Id:               g.Id,
		Version:          g.Version,
		Players:          g.Players,
		PeekedAtDrawPile: false,
		WhoseTurn:        newWhoseTurn,
		WhoKnocked:       -1,
	}, nil
}

func (g *GameState) SwapForDrawPile(player int, position Position) (*GameState, error) {
	if g.IsOver() {
		return nil, errors.New("game is over")
	}
	if !g.AllPlayersPresent() {
		return nil, errors.New("not all players have joined")
	}
	if g.WhoseTurn != player {
		return nil, errors.New("not your turn")
	}

	// update draw pile
	var updatedDrawPile = g.DrawPile
	updatedDrawPtr := &updatedDrawPile
	toSwapIntoHand := updatedDrawPtr.RemoveLast()

	// update current player
	var currentPlayer = g.Players[player]
	currentPlayerPtr := &currentPlayer
	toSwapOutOfHand := currentPlayerPtr.CardAt(position)
	currentPlayerPtr.SwapIntoHand(toSwapIntoHand, position)
	g.Players[player] = *currentPlayerPtr

	// update discard pile
	var updatedDiscardPile = g.DiscardPile
	updatedDiscardPtr := &updatedDiscardPile
	updatedDiscardPtr.AddLast(toSwapOutOfHand)

	// update whose turn it is
	newWhoseTurn := (g.WhoseTurn + 1) % len(g.Players)

	return &GameState{
		DrawPile:         updatedDrawPile,
		DiscardPile:      updatedDiscardPile,
		Id:               g.Id,
		Version:          g.Version,
		Players:          g.Players,
		PeekedAtDrawPile: false,
		WhoseTurn:        newWhoseTurn,
		WhoKnocked:       -1,
	}, nil
}
