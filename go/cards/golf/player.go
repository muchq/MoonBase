package golf

import (
	"github.com/muchq/moonbase/go/cards"
	"slices"
	"strconv"
)

type Position string

const (
	TopLeft     Position = "TOP_LEFT"
	TopRight    Position = "TOP_RIGHT"
	BottomLeft  Position = "BOTTOM_LEFT"
	BottomRight Position = "BOTTOM_RIGHT"
)

type Player struct {
	Id       string
	Username string
	Cards    map[Position]cards.Card
}

func valueToPoints(value cards.Value) int {
	v, err := strconv.Atoi(string(value))
	if err == nil {
		return v
	} else if value == "A" {
		return 1
	} else if value == "J" {
		return 0
	} else {
		return 10
	}
}

func (p *Player) Score() int {
	var values []cards.Value
	for _, card := range p.Cards {
		values = append(values, card.Value)
	}
	slices.Sort(values)

	score := 0
	for i := 0; i < len(values); {
		if i < len(values)-1 && values[i] == values[i+1] {
			i += 1
		} else {
			score += valueToPoints(values[i])
		}
		i += 1
	}
	return score
}

func (p *Player) CardAt(position Position) cards.Card {
	return p.Cards[position]
}

func (p *Player) SwapIntoHand(card cards.Card, position Position) {
	p.Cards[position] = card
}
