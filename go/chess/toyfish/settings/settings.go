package settings

import (
	"encoding/json"
	"os"
	"path/filepath"
)

type Color int

const (
	White Color = 0
	Black       = 1
	None        = -1
)

type Piece string

type Settings struct {
	Fen         string           `json:"fen"`
	Directions  map[Piece][]int  `json:"directions"`
	Colors      map[Piece]Color  `json:"colors"`
	Weights     map[Piece]int    `json:"weights"`
	Pst         []int            `json:"pst"`
	Rank2       []int            `json:"rank_2"`
	Rank7       []int            `json:"rank_7"`
	Coordinates []string         `json:"coordinates"`
	Pieces      map[Piece]string `json:"pieces"`
}

func NewSettings() (*Settings, error) {
	const settingsJson = "go/chess/toyfish/settings/settings.json"
	abs, err := filepath.Abs(settingsJson)
	if err != nil {
		return nil, err
	}

	content, err := os.ReadFile(abs)

	if err != nil {
		return nil, err
	}

	var settings Settings
	err = json.Unmarshal(content, &settings)
	if err != nil {
		return nil, err
	}

	return &settings, nil
}
