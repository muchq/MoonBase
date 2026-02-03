package com.muchq.pgn.lexer;

import org.junit.Test;

import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class PgnLexerTest {

    // === Basic Token Tests ===

    @Test
    public void tokenize_empty() {
        List<Token> tokens = new PgnLexer("").tokenize();
        assertThat(tokens).hasSize(1);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.EOF);
    }

    @Test
    public void tokenize_whitespaceOnly() {
        List<Token> tokens = new PgnLexer("   \n\t  ").tokenize();
        assertThat(tokens).hasSize(1);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.EOF);
    }

    // === Delimiter Tests ===

    @Test
    public void tokenize_brackets() {
        List<Token> tokens = new PgnLexer("[]").tokenize();
        assertThat(tokens).hasSize(3);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.LEFT_BRACKET);
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.RIGHT_BRACKET);
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.EOF);
    }

    @Test
    public void tokenize_parens() {
        List<Token> tokens = new PgnLexer("()").tokenize();
        assertThat(tokens).hasSize(3);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.LEFT_PAREN);
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.RIGHT_PAREN);
    }

    // === String Tests ===

    @Test
    public void tokenize_simpleString() {
        List<Token> tokens = new PgnLexer("\"hello\"").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.STRING);
        assertThat(tokens.get(0).value()).isEqualTo("hello");
    }

    @Test
    public void tokenize_stringWithSpaces() {
        List<Token> tokens = new PgnLexer("\"World Championship\"").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.STRING);
        assertThat(tokens.get(0).value()).isEqualTo("World Championship");
    }

    @Test
    public void tokenize_stringWithEscapedQuote() {
        List<Token> tokens = new PgnLexer("\"say \\\"hi\\\"\"").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.STRING);
        assertThat(tokens.get(0).value()).isEqualTo("say \"hi\"");
    }

    @Test
    public void tokenize_stringWithBackslash() {
        List<Token> tokens = new PgnLexer("\"path\\\\file\"").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).value()).isEqualTo("path\\file");
    }

    @Test
    public void tokenize_unterminatedString() {
        assertThatThrownBy(() -> new PgnLexer("\"unterminated").tokenize())
            .isInstanceOf(LexerException.class)
            .hasMessageContaining("Unterminated string");
    }

    // === Integer Tests ===

    @Test
    public void tokenize_integer() {
        List<Token> tokens = new PgnLexer("42").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.INTEGER);
        assertThat(tokens.get(0).value()).isEqualTo("42");
    }

    @Test
    public void tokenize_moveNumber() {
        List<Token> tokens = new PgnLexer("1.").tokenize();
        assertThat(tokens).hasSize(3);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.INTEGER);
        assertThat(tokens.get(0).value()).isEqualTo("1");
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.PERIOD);
    }

    @Test
    public void tokenize_multiDigitMoveNumber() {
        List<Token> tokens = new PgnLexer("15.").tokenize();
        assertThat(tokens).hasSize(3);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.INTEGER);
        assertThat(tokens.get(0).value()).isEqualTo("15");
    }

    // === Period and Ellipsis Tests ===

    @Test
    public void tokenize_period() {
        List<Token> tokens = new PgnLexer(".").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.PERIOD);
    }

    @Test
    public void tokenize_ellipsis() {
        List<Token> tokens = new PgnLexer("...").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.ELLIPSIS);
        assertThat(tokens.get(0).value()).isEqualTo("...");
    }

    @Test
    public void tokenize_blackMoveNumber() {
        List<Token> tokens = new PgnLexer("1...").tokenize();
        assertThat(tokens).hasSize(3);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.INTEGER);
        assertThat(tokens.get(0).value()).isEqualTo("1");
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.ELLIPSIS);
    }

    // === Symbol Tests (moves and tag names) ===

    @Test
    public void tokenize_pawnMove() {
        List<Token> tokens = new PgnLexer("e4").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("e4");
    }

    @Test
    public void tokenize_pieceMove() {
        List<Token> tokens = new PgnLexer("Nf3").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("Nf3");
    }

    @Test
    public void tokenize_capture() {
        List<Token> tokens = new PgnLexer("Bxe5").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("Bxe5");
    }

    @Test
    public void tokenize_pawnCapture() {
        List<Token> tokens = new PgnLexer("exd5").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("exd5");
    }

    @Test
    public void tokenize_castleKingside() {
        List<Token> tokens = new PgnLexer("O-O").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("O-O");
    }

    @Test
    public void tokenize_castleQueenside() {
        List<Token> tokens = new PgnLexer("O-O-O").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("O-O-O");
    }

    @Test
    public void tokenize_check() {
        List<Token> tokens = new PgnLexer("Qh7+").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("Qh7+");
    }

    @Test
    public void tokenize_checkmate() {
        List<Token> tokens = new PgnLexer("Qxf7#").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("Qxf7#");
    }

    @Test
    public void tokenize_promotion() {
        List<Token> tokens = new PgnLexer("e8=Q").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("e8=Q");
    }

    @Test
    public void tokenize_promotionWithCheck() {
        List<Token> tokens = new PgnLexer("e8=Q+").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("e8=Q+");
    }

    @Test
    public void tokenize_disambiguatedMove_file() {
        List<Token> tokens = new PgnLexer("Rae1").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("Rae1");
    }

    @Test
    public void tokenize_disambiguatedMove_rank() {
        List<Token> tokens = new PgnLexer("R1e4").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("R1e4");
    }

    @Test
    public void tokenize_disambiguatedMove_full() {
        List<Token> tokens = new PgnLexer("Qd1e2").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("Qd1e2");
    }

    @Test
    public void tokenize_tagName() {
        List<Token> tokens = new PgnLexer("Event").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(0).value()).isEqualTo("Event");
    }

    // === Comment Tests ===

    @Test
    public void tokenize_comment() {
        List<Token> tokens = new PgnLexer("{this is a comment}").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.COMMENT);
        assertThat(tokens.get(0).value()).isEqualTo("this is a comment");
    }

    @Test
    public void tokenize_emptyComment() {
        List<Token> tokens = new PgnLexer("{}").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.COMMENT);
        assertThat(tokens.get(0).value()).isEqualTo("");
    }

    @Test
    public void tokenize_multilineComment() {
        List<Token> tokens = new PgnLexer("{line one\nline two}").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.COMMENT);
        assertThat(tokens.get(0).value()).isEqualTo("line one\nline two");
    }

    @Test
    public void tokenize_unterminatedComment() {
        assertThatThrownBy(() -> new PgnLexer("{unclosed").tokenize())
            .isInstanceOf(LexerException.class)
            .hasMessageContaining("Unterminated comment");
    }

    // === NAG Tests ===

    @Test
    public void tokenize_nag() {
        List<Token> tokens = new PgnLexer("$1").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.NAG);
        assertThat(tokens.get(0).value()).isEqualTo("$1");
    }

    @Test
    public void tokenize_multiDigitNag() {
        List<Token> tokens = new PgnLexer("$142").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.NAG);
        assertThat(tokens.get(0).value()).isEqualTo("$142");
    }

    @Test
    public void tokenize_nagWithoutNumber() {
        assertThatThrownBy(() -> new PgnLexer("$").tokenize())
            .isInstanceOf(LexerException.class);
    }

    // === Result Tests ===

    @Test
    public void tokenize_whiteWins() {
        List<Token> tokens = new PgnLexer("1-0").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.RESULT);
        assertThat(tokens.get(0).value()).isEqualTo("1-0");
    }

    @Test
    public void tokenize_blackWins() {
        List<Token> tokens = new PgnLexer("0-1").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.RESULT);
        assertThat(tokens.get(0).value()).isEqualTo("0-1");
    }

    @Test
    public void tokenize_draw() {
        List<Token> tokens = new PgnLexer("1/2-1/2").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.RESULT);
        assertThat(tokens.get(0).value()).isEqualTo("1/2-1/2");
    }

    @Test
    public void tokenize_ongoing() {
        List<Token> tokens = new PgnLexer("*").tokenize();
        assertThat(tokens).hasSize(2);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.RESULT);
        assertThat(tokens.get(0).value()).isEqualTo("*");
    }

    // === Tag Pair Tests ===

    @Test
    public void tokenize_tagPair() {
        List<Token> tokens = new PgnLexer("[Event \"Test\"]").tokenize();
        assertThat(tokens).hasSize(5);
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.LEFT_BRACKET);
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(1).value()).isEqualTo("Event");
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.STRING);
        assertThat(tokens.get(2).value()).isEqualTo("Test");
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.RIGHT_BRACKET);
        assertThat(tokens.get(4).type()).isEqualTo(TokenType.EOF);
    }

    // === Movetext Tests ===

    @Test
    public void tokenize_simpleMovetext() {
        List<Token> tokens = new PgnLexer("1. e4 e5 2. Nf3").tokenize();
        assertThat(tokens).hasSize(9);
        // 1
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.INTEGER);
        assertThat(tokens.get(0).value()).isEqualTo("1");
        // .
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.PERIOD);
        // e4
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(2).value()).isEqualTo("e4");
        // e5
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(3).value()).isEqualTo("e5");
        // 2
        assertThat(tokens.get(4).type()).isEqualTo(TokenType.INTEGER);
        // .
        assertThat(tokens.get(5).type()).isEqualTo(TokenType.PERIOD);
        // Nf3
        assertThat(tokens.get(6).type()).isEqualTo(TokenType.SYMBOL);
        assertThat(tokens.get(6).value()).isEqualTo("Nf3");
    }

    @Test
    public void tokenize_movetextWithComment() {
        List<Token> tokens = new PgnLexer("1. e4 {King's pawn} e5").tokenize();
        assertThat(tokens).hasSize(6);
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.COMMENT);
        assertThat(tokens.get(3).value()).isEqualTo("King's pawn");
        assertThat(tokens.get(4).type()).isEqualTo(TokenType.SYMBOL);
    }

    @Test
    public void tokenize_movetextWithNag() {
        List<Token> tokens = new PgnLexer("1. e4 $1 e5").tokenize();
        assertThat(tokens).hasSize(6);
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.NAG);
        assertThat(tokens.get(3).value()).isEqualTo("$1");
    }

    @Test
    public void tokenize_movetextWithVariation() {
        List<Token> tokens = new PgnLexer("1. e4 (1. d4) e5").tokenize();
        assertThat(tokens).hasSize(10);
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.LEFT_PAREN);
        assertThat(tokens.get(7).type()).isEqualTo(TokenType.RIGHT_PAREN);
    }

    // === Position Tracking Tests ===

    @Test
    public void tokenize_trackLineNumber() {
        List<Token> tokens = new PgnLexer("a\nb\nc").tokenize();
        assertThat(tokens.get(0).line()).isEqualTo(1);
        assertThat(tokens.get(1).line()).isEqualTo(2);
        assertThat(tokens.get(2).line()).isEqualTo(3);
    }

    @Test
    public void tokenize_trackColumn() {
        List<Token> tokens = new PgnLexer("abc def").tokenize();
        assertThat(tokens.get(0).column()).isEqualTo(1);
        assertThat(tokens.get(1).column()).isEqualTo(5);
    }

    // === Full Game Tokenization ===

    @Test
    public void tokenize_completeGame() {
        String pgn = """
            [Event "Test"]
            [Site "Home"]
            [Result "1-0"]

            1. e4 e5 2. Nf3 Nc6 1-0
            """;
        List<Token> tokens = new PgnLexer(pgn).tokenize();

        // Should have: 3 tags (each: [ SYMBOL STRING ]) + movetext + result + EOF
        assertThat(tokens).isNotEmpty();
        assertThat(tokens.get(tokens.size() - 1).type()).isEqualTo(TokenType.EOF);

        // Verify tag structure
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.LEFT_BRACKET);
        assertThat(tokens.get(1).value()).isEqualTo("Event");
        assertThat(tokens.get(2).value()).isEqualTo("Test");
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.RIGHT_BRACKET);
    }

    // === Line Comment Tests (semicolon) ===

    @Test
    public void tokenize_lineComment() {
        List<Token> tokens = new PgnLexer("e4 ; this is a line comment\ne5").tokenize();
        // Line comments should be skipped (or tokenized as COMMENT depending on implementation)
        // For this test, we expect comments to be ignored
        assertThat(tokens.stream().filter(t -> t.type() == TokenType.SYMBOL).count()).isEqualTo(2);
    }

    // === Edge Cases ===

    @Test
    public void tokenize_moveWithInlineAnnotation() {
        // Some PGN files use ! and ? directly after moves
        // These could be parsed as part of the symbol or as separate NAGs
        List<Token> tokens = new PgnLexer("e4!").tokenize();
        // Accept either: symbol "e4!" or symbol "e4" + something
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
    }

    @Test
    public void tokenize_moveWithDoubleAnnotation() {
        List<Token> tokens = new PgnLexer("e4!!").tokenize();
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
    }

    @Test
    public void tokenize_moveWithQuestionMark() {
        List<Token> tokens = new PgnLexer("Qxf7??").tokenize();
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
    }

    @Test
    public void tokenize_moveWithMixedAnnotation() {
        List<Token> tokens = new PgnLexer("Nc3!?").tokenize();
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.SYMBOL);
    }
}
