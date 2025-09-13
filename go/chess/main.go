package main

import (
	"errors"
	"fmt"
	"strings"
	"unicode"

	ch "github.com/muchq/moonbase/go/chess"
	s "github.com/muchq/moonbase/go/chess/settings"
)

type Chess struct {
	Board    string
	Settings *s.Settings
	ToMove   s.Color
}

func (c *Chess) PrintBoard() {
	b := ""
	for _, p := range c.Board {
		if p == ' ' || p == '.' || p == '\n' {
			b += string(p)
		} else {
			b += c.Settings.Pieces[s.Piece(p)]
		}
	}
	fmt.Println(b)
}

func (c *Chess) pieceMightMove(p s.Piece, color s.Color) bool {
	co, exists := c.Settings.Colors[p]
	if exists {
		return co == color
	}
	return false
}

func (c *Chess) indexToSquare(idx int) string {
	return c.Settings.Coordinates[idx]
}

func (c *Chess) pieceToGlyph(p s.Piece) string {
	return c.Settings.Pieces[p]
}

func (c *Chess) GenerateMoves() {
	fmt.Println("generating moves...")
	for square, p := range c.Board {
		piece := s.Piece(p)
		if c.pieceMightMove(piece, c.ToMove) {
			for _, offset := range c.Settings.Directions[piece] {
				fmt.Printf("piece %s on square %s with offset %d\n", c.pieceToGlyph(piece), c.indexToSquare(square), offset)
				targetSquare := square
				for true {
					targetSquare += offset
					// capturedPiece := s.Piece(c.Board[targetSquare])
				}
			}
		}
	}
}

func FenToBoard(settings *s.Settings) (string, error) {
	emptyLine := "         \n"
	rawBoard := strings.ReplaceAll(strings.Split(settings.Fen, " ")[0], "/", "\n ")
	board := emptyLine
	board += emptyLine
	board += " "
	for _, r := range rawBoard {
		if unicode.IsDigit(r) {
			for _ = range int(r - '0') {
				board += "."
			}
		} else {
			board += string(r)
		}
	}

	board += "\n"
	board += emptyLine
	board += emptyLine

	if len(board) != 120 {
		return "", errors.New(fmt.Sprintf("board length must be 120 chars. actual length %d", len(board)))
	}
	return board, nil
}

func WhoseMove(settings *s.Settings) (s.Color, error) {
	rawMove := strings.Split(settings.Fen, " ")[1]

	switch rawMove {
	case "w":
		{
			return s.White, nil
		}
	case "b":
		{
			return s.Black, nil
		}
	default:
		return s.None, errors.New("invalid move string in fen: " + rawMove)
	}
}

func NewChess(settings *s.Settings) (*Chess, error) {
	chess := &Chess{Settings: settings}
	textBoard, err := FenToBoard(settings)
	if err != nil {
		return nil, err
	}
	chess.Board = textBoard

	whoseMove, err := WhoseMove(settings)
	if err != nil {
		return nil, err
	}
	chess.ToMove = whoseMove

	return chess, nil
}

func main() {
	settings, err := s.NewSettings()
	if err != nil {
		fmt.Println("failed to read settings: ", err)
		return
	}

	//chess, err := NewChess(settings)
	//if err != nil {
	//	fmt.Println("failed to create chess: ", err)
	//	return
	//}
	//
	//chess.PrintBoard()
	//if chess.ToMove == s.White {
	//	fmt.Println("White To Move")
	//} else {
	//	fmt.Println("Black To Move")
	//}

	game, err := ch.NewGame(settings)
	if err != nil {
		fmt.Println("failed to create game: ", err)
		return
	}

	fmt.Println(game.String())

	game.GenerateMoves()
}
