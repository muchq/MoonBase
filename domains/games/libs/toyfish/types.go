package toyfish

import (
	"errors"
	"fmt"
	"slices"
	"strings"
	"unicode"

	s "github.com/muchq/moonbase/domains/games/libs/toyfish/settings"
)

type Color int

const (
	None  Color = -1
	White       = 0
	Black       = 1
)

type Piece struct {
	FenRepr int8
	Glyph   rune
	Value   int
	Side    Color
}

type PieceInfo struct {
	F int8
	G [2]rune
	S int
}

var pieceInfos = []PieceInfo{
	PieceInfo{F: 'P', G: [2]rune{'♙', '♟'}, S: 100},
	PieceInfo{F: 'N', G: [2]rune{'♘', '♞'}, S: 300},
	PieceInfo{F: 'B', G: [2]rune{'♗', '♝'}, S: 300},
	PieceInfo{F: 'R', G: [2]rune{'♖', '♜'}, S: 500},
	PieceInfo{F: 'Q', G: [2]rune{'♕', '♛'}, S: 900},
	PieceInfo{F: 'K', G: [2]rune{'♔', '♚'}, S: 10000},
}

type Board [120]*Piece

func populatePieceMap() map[int8]*Piece {
	pieceMap := make(map[int8]*Piece, 12)

	for _, pieceInfo := range pieceInfos {
		pieceMap[pieceInfo.F] = &Piece{
			FenRepr: pieceInfo.F,
			Glyph:   pieceInfo.G[0],
			Side:    White,
			Value:   pieceInfo.S,
		}
		lowerCased := int8(strings.ToLower(string(pieceInfo.F))[0])
		pieceMap[lowerCased] = &Piece{
			FenRepr: lowerCased,
			Glyph:   pieceInfo.G[1],
			Side:    Black,
			Value:   pieceInfo.S * -1,
		}
	}

	return pieceMap
}

type Game struct {
	StartingFen string
	Settings    *s.Settings
	Board       Board
	Side        Color
}

func FenToBoard(fen string, pieceMap map[int8]*Piece) (Board, error) {
	var board Board
	i := 21
	rawBoard := strings.ReplaceAll(strings.Split(fen, " ")[0], "/", "\n ")
	for _, r := range rawBoard {
		if unicode.IsDigit(r) {
			i += int(r - '0')
		} else if unicode.IsSpace(r) {
			i++
		} else {
			p, exists := pieceMap[int8(r)]
			if !exists {
				return board, errors.New(fmt.Sprintf("Unknown piece: %d", r))
			}
			board[i] = p
			i++
		}
	}

	return board, nil
}

func (g *Game) String() string {
	text := ""
	for row := range 12 {
		for col := range 10 {
			piece := g.Board[row*10+col]
			if col == 9 {
				text += "\n"
			} else if row < 2 || row > 9 || col == 0 {
				text += " "
			} else if piece == nil {
				text += "."
			} else {
				text += string(piece.Glyph)
			}
		}
	}
	return text
}

func (g *Game) Fen() string {
	return g.StartingFen
}

type Move struct {
	Source        int
	Target        int
	Piece         *Piece
	CapturedPiece *Piece
}

func (m Move) String() string {
	return fmt.Sprintf("%d:%d:%d", m.Source, m.Target, m.Piece)
}

func (g *Game) GenerateMoves() []Move {
	fmt.Println("generating moves...")
	moveList := make([]Move, 0)
	for idx, piece := range g.Board {
		//square := g.Settings.Coordinates[idx]
		if piece != nil && piece.Side == g.Side {
			for _, offset := range g.Settings.Directions[s.Piece(piece.FenRepr)] {
				//fmt.Printf("piece %c on square %s with offset %d\n", piece.Glyph, square, offset)
				targetSquare := idx
				for {
					targetSquare += offset
					capturedPiece := g.Board[targetSquare]
					// we're off the board
					if g.Settings.Coordinates[targetSquare] == "xx" {
						break
					}
					// we've run into our own piece
					if capturedPiece != nil && capturedPiece.Side == g.Side {
						break
					}

					// ***************************************************
					// ******************* PAWN LOGIC ********************
					// ***************************************************
					if strings.Contains("pP", string(piece.FenRepr)) {
						// no en-passant on empty square
						if offset%10 != 0 && capturedPiece == nil {
							break
						}
						// pawns can't capture forward
						if offset%10 == 0 && capturedPiece != nil {
							break
						}
					}
					// pawns can't move 2 squares over another piece
					if piece.FenRepr == 'P' && offset == -20 {
						if !slices.Contains(g.Settings.Rank2, idx) {
							break
						}
						if g.Board[idx-10] != nil {
							break
						}
					}
					if piece.FenRepr == 'p' && offset == 20 {
						if !slices.Contains(g.Settings.Rank7, idx) {
							break
						}
						if g.Board[idx+10] != nil {
							break
						}
					}
					// TODO: implement es-passant capture suppoer

					// TODO: implement castling rules

					// ***************************************************
					// ******************* CHECKMATE *********************
					// ***************************************************
					if capturedPiece != nil && strings.Contains("kK", string(capturedPiece.FenRepr)) {
						return nil
					}

					moveList = append(moveList, Move{
						Source:        idx,
						Target:        targetSquare,
						Piece:         piece,
						CapturedPiece: capturedPiece,
					})

					//g.Board[targetSquare] = piece
					//g.Board[idx] = nil
					//fmt.Println(g.String())
					//g.Board[targetSquare] = capturedPiece
					//g.Board[idx] = piece

					// we've run into an opponent's piece
					if capturedPiece != nil && capturedPiece.Side != g.Side {
						break
					}

					// pawn, knight, and king aren't sliding pieces (only one move at a time in each allowed direction)
					if strings.Contains("pPnNkK", string(piece.FenRepr)) {
						break
					}
				}
			}
		}
	}
	return moveList
}

func NewGame(settings *s.Settings) (*Game, error) {
	pieceMap := populatePieceMap()
	board, err := FenToBoard(settings.Fen, pieceMap)
	if err != nil {
		return nil, err
	}
	game := Game{
		StartingFen: settings.Fen,
		Settings:    settings,
		Board:       board,
		Side:        White,
	}

	return &game, nil
}
