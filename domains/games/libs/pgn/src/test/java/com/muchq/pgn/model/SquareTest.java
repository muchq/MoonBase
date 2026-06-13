package com.muchq.pgn.model;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.Test;

public class SquareTest {

  @Test
  public void parse_e4() {
    Square square = Square.parse("e4");
    assertThat(square.file()).isEqualTo(File.E);
    assertThat(square.rank()).isEqualTo(Rank.R4);
  }

  @Test
  public void parse_a1() {
    Square square = Square.parse("a1");
    assertThat(square.file()).isEqualTo(File.A);
    assertThat(square.rank()).isEqualTo(Rank.R1);
  }

  @Test
  public void parse_h8() {
    Square square = Square.parse("h8");
    assertThat(square.file()).isEqualTo(File.H);
    assertThat(square.rank()).isEqualTo(Rank.R8);
  }

  @Test
  public void parse_uppercase() {
    Square square = Square.parse("E4");
    assertThat(square.file()).isEqualTo(File.E);
    assertThat(square.rank()).isEqualTo(Rank.R4);
  }

  @Test
  public void parse_invalidFile() {
    assertThatThrownBy(() -> Square.parse("z4")).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void parse_invalidRank() {
    assertThatThrownBy(() -> Square.parse("e9")).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void parse_tooShort() {
    assertThatThrownBy(() -> Square.parse("e")).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void parse_tooLong() {
    assertThatThrownBy(() -> Square.parse("e44")).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void toString_e4() {
    Square square = new Square(File.E, Rank.R4);
    assertThat(square.toString()).isEqualTo("e4");
  }

  @Test
  public void toString_a1() {
    Square square = new Square(File.A, Rank.R1);
    assertThat(square.toString()).isEqualTo("a1");
  }

  @Test
  public void toString_h8() {
    Square square = new Square(File.H, Rank.R8);
    assertThat(square.toString()).isEqualTo("h8");
  }

  // File enum tests

  @Test
  public void file_fromChar() {
    assertThat(File.fromChar('a')).isEqualTo(File.A);
    assertThat(File.fromChar('h')).isEqualTo(File.H);
    assertThat(File.fromChar('E')).isEqualTo(File.E);
  }

  @Test
  public void file_fromChar_invalid() {
    assertThatThrownBy(() -> File.fromChar('z')).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void file_toChar() {
    assertThat(File.A.toChar()).isEqualTo('a');
    assertThat(File.H.toChar()).isEqualTo('h');
  }

  // Rank enum tests

  @Test
  public void rank_fromNumber() {
    assertThat(Rank.fromNumber(1)).isEqualTo(Rank.R1);
    assertThat(Rank.fromNumber(8)).isEqualTo(Rank.R8);
  }

  @Test
  public void rank_fromNumber_invalid() {
    assertThatThrownBy(() -> Rank.fromNumber(0)).isInstanceOf(IllegalArgumentException.class);
    assertThatThrownBy(() -> Rank.fromNumber(9)).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void rank_fromChar() {
    assertThat(Rank.fromChar('1')).isEqualTo(Rank.R1);
    assertThat(Rank.fromChar('8')).isEqualTo(Rank.R8);
  }

  @Test
  public void rank_toChar() {
    assertThat(Rank.R1.toChar()).isEqualTo('1');
    assertThat(Rank.R8.toChar()).isEqualTo('8');
  }

  @Test
  public void rank_number() {
    assertThat(Rank.R1.number()).isEqualTo(1);
    assertThat(Rank.R8.number()).isEqualTo(8);
  }

  // Piece enum tests

  @Test
  public void piece_fromSymbol() {
    assertThat(Piece.fromSymbol('K')).isEqualTo(Piece.KING);
    assertThat(Piece.fromSymbol('Q')).isEqualTo(Piece.QUEEN);
    assertThat(Piece.fromSymbol('R')).isEqualTo(Piece.ROOK);
    assertThat(Piece.fromSymbol('B')).isEqualTo(Piece.BISHOP);
    assertThat(Piece.fromSymbol('N')).isEqualTo(Piece.KNIGHT);
  }

  @Test
  public void piece_fromSymbol_invalid() {
    assertThatThrownBy(() -> Piece.fromSymbol('X')).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void piece_symbol() {
    assertThat(Piece.KING.symbol()).isEqualTo('K');
    assertThat(Piece.PAWN.symbol()).isEqualTo('\0');
  }

  // Nag tests

  @Test
  public void nag_parse() {
    assertThat(Nag.parse("$1").value()).isEqualTo(1);
    assertThat(Nag.parse("$6").value()).isEqualTo(6);
    assertThat(Nag.parse("$142").value()).isEqualTo(142);
  }

  @Test
  public void nag_parse_invalid() {
    assertThatThrownBy(() -> Nag.parse("1")).isInstanceOf(IllegalArgumentException.class);
    assertThatThrownBy(() -> Nag.parse("$")).isInstanceOf(IllegalArgumentException.class);
    assertThatThrownBy(() -> Nag.parse("$abc")).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void nag_toString() {
    assertThat(new Nag(1).toString()).isEqualTo("$1");
    assertThat(new Nag(142).toString()).isEqualTo("$142");
  }
}
