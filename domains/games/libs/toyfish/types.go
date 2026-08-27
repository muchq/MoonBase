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

type CastlingRights uint8

const (
	CastlingWhiteKingSide  CastlingRights = 1 << 0
	CastlingWhiteQueenSide CastlingRights = 1 << 1
	CastlingBlackKingSide  CastlingRights = 1 << 2
	CastlingBlackQueenSide CastlingRights = 1 << 3
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
		lowerCased := int8(strings.ToLower(string(rune(pieceInfo.F)))[0])
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
	Castling    CastlingRights
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

func (g *Game) IsAttacked(square int, attackerSide Color) bool {
	// Check for attacks from sliding pieces (Rook, Queen)
	for _, offset := range g.Settings.Directions["R"] {
		target := square
		for {
			target += offset
			if g.Settings.Coordinates[target] == "xx" {
				break
			}
			p := g.Board[target]
			if p != nil {
				if p.Side == attackerSide && strings.Contains("rRqQ", string(rune(p.FenRepr))) {
					return true
				}
				break
			}
		}
	}

	// Check for attacks from sliding pieces (Bishop, Queen)
	for _, offset := range g.Settings.Directions["B"] {
		target := square
		for {
			target += offset
			if g.Settings.Coordinates[target] == "xx" {
				break
			}
			p := g.Board[target]
			if p != nil {
				if p.Side == attackerSide && strings.Contains("bBqQ", string(rune(p.FenRepr))) {
					return true
				}
				break
			}
		}
	}

	// Check for attacks from Knights
	for _, offset := range g.Settings.Directions["N"] {
		target := square + offset
		if g.Settings.Coordinates[target] != "xx" {
			p := g.Board[target]
			if p != nil && p.Side == attackerSide && strings.Contains("nN", string(rune(p.FenRepr))) {
				return true
			}
		}
	}

	// Check for attacks from Kings
	for _, offset := range g.Settings.Directions["K"] {
		target := square + offset
		if g.Settings.Coordinates[target] != "xx" {
			p := g.Board[target]
			if p != nil && p.Side == attackerSide && strings.Contains("kK", string(rune(p.FenRepr))) {
				return true
			}
		}
	}

	// Check for attacks from Pawns
	// White pawns attack up (-9, -11). So if attacker is White, look down (+9, +11).
	// Black pawns attack down (+9, +11). So if attacker is Black, look up (-9, -11).
	var pawnOffsets []int
	var pawnChar string
	if attackerSide == White {
		pawnOffsets = []int{9, 11}
		pawnChar = "P"
	} else {
		pawnOffsets = []int{-9, -11}
		pawnChar = "p"
	}

	for _, offset := range pawnOffsets {
		target := square + offset
		if g.Settings.Coordinates[target] != "xx" {
			p := g.Board[target]
			if p != nil && p.Side == attackerSide && string(rune(p.FenRepr)) == pawnChar {
				return true
			}
		}
	}

	return false
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
			for _, offset := range g.Settings.Directions[s.Piece(rune(piece.FenRepr))] {
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
					if strings.Contains("pP", string(rune(piece.FenRepr))) {
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

					// Castling Logic
					if (piece.FenRepr == 'K' || piece.FenRepr == 'k') {
						if piece.Side == White && idx == 95 {
							// King Side (e1 -> g1)
							if g.Castling&CastlingWhiteKingSide != 0 {
								// Check path empty: f1(96), g1(97)
								if g.Board[96] == nil && g.Board[97] == nil {
									// Check safe: e1(idx), f1(96), g1(97)
									if !g.IsAttacked(idx, Black) && !g.IsAttacked(96, Black) && !g.IsAttacked(97, Black) {
										moveList = append(moveList, Move{
											Source:        idx,
											Target:        97,
											Piece:         piece,
											CapturedPiece: nil,
										})
									}
								}
							}
							// Queen Side (e1 -> c1)
							if g.Castling&CastlingWhiteQueenSide != 0 {
								// Check path empty: d1(94), c1(93), b1(92)
								if g.Board[94] == nil && g.Board[93] == nil && g.Board[92] == nil {
									// Check safe: e1(idx), d1(94), c1(93)
									if !g.IsAttacked(idx, Black) && !g.IsAttacked(94, Black) && !g.IsAttacked(93, Black) {
										moveList = append(moveList, Move{
											Source:        idx,
											Target:        93,
											Piece:         piece,
											CapturedPiece: nil,
										})
									}
								}
							}
						} else if piece.Side == Black && idx == 25 {
							// Black King Side (e8 -> g8)
							if g.Castling&CastlingBlackKingSide != 0 {
								// Check path empty: f8(26), g8(27)
								if g.Board[26] == nil && g.Board[27] == nil {
									// Check safe: e8(idx), f8(26), g8(27)
									if !g.IsAttacked(idx, White) && !g.IsAttacked(26, White) && !g.IsAttacked(27, White) {
										moveList = append(moveList, Move{
											Source:        idx,
											Target:        27,
											Piece:         piece,
											CapturedPiece: nil,
										})
									}
								}
							}
							// Black Queen Side (e8 -> c8)
							if g.Castling&CastlingBlackQueenSide != 0 {
								// Check path empty: d8(24), c8(23), b8(22)
								if g.Board[24] == nil && g.Board[23] == nil && g.Board[22] == nil {
									// Check safe: e8(idx), d8(24), c8(23)
									if !g.IsAttacked(idx, White) && !g.IsAttacked(24, White) && !g.IsAttacked(23, White) {
										moveList = append(moveList, Move{
											Source:        idx,
											Target:        23,
											Piece:         piece,
											CapturedPiece: nil,
										})
									}
								}
							}
						}
					}

					// ***************************************************
					// ******************* CHECKMATE *********************
					// ***************************************************
					if capturedPiece != nil && strings.Contains("kK", string(rune(capturedPiece.FenRepr))) {
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
					if strings.Contains("pPnNkK", string(rune(piece.FenRepr))) {
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

	parts := strings.Split(settings.Fen, " ")
	var side Color = White
	if len(parts) >= 2 {
		if parts[1] == "b" {
			side = Black
		}
	}

	var castling CastlingRights
	if len(parts) >= 3 {
		c := parts[2]
		if strings.Contains(c, "K") {
			castling |= CastlingWhiteKingSide
		}
		if strings.Contains(c, "Q") {
			castling |= CastlingWhiteQueenSide
		}
		if strings.Contains(c, "k") {
			castling |= CastlingBlackKingSide
		}
		if strings.Contains(c, "q") {
			castling |= CastlingBlackQueenSide
		}
	}

	game := Game{
		StartingFen: settings.Fen,
		Settings:    settings,
		Board:       board,
		Side:        side,
		Castling:    castling,
	}

	return &game, nil
}
