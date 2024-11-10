package cards

import "math/rand"

type Suit string

var Suits = []Suit{"C", "D", "H", "S"}

type Value string

var Values = []Value{"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"}

type Card struct {
	Suit  Suit  `json:"suit"`
	Value Value `json:"value"`
}

type Deck struct {
	Cards []Card
}

func (d *Deck) Shuffle() {
	rand.Shuffle(len(d.Cards), func(i, j int) {
		d.Cards[i], d.Cards[j] = d.Cards[j], d.Cards[i]
	})
}

func (d *Deck) IsEmpty() bool {
	return len(d.Cards) == 0
}

func (d *Deck) AddLast(toAdd Card) {
	d.Cards = append(d.Cards, toAdd)
}

func (d *Deck) RemoveLast() Card {
	lastCard := d.Cards[len(d.Cards)-1]
	d.Cards = d.Cards[:len(d.Cards)-1]
	return lastCard
}

func NewDeck() *Deck {
	deck := Deck{}
	for sIndex := 0; sIndex < len(Suits); sIndex++ {
		for vIndex := 0; vIndex < len(Values); vIndex++ {
			deck.Cards = append(deck.Cards, Card{
				Suit:  Suits[sIndex],
				Value: Values[vIndex],
			})
		}
	}
	return &deck
}
