package com.muchq.one_d4.chessql.lexer;

import org.junit.Test;

import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class LexerTest {

    @Test
    public void testSimpleComparison() {
        List<Token> tokens = new Lexer("white_elo >= 2500").tokenize();
        assertThat(tokens).hasSize(4); // IDENTIFIER, GTE, NUMBER, EOF
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.IDENTIFIER);
        assertThat(tokens.get(0).value()).isEqualTo("white_elo");
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.GTE);
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.NUMBER);
        assertThat(tokens.get(2).value()).isEqualTo("2500");
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.EOF);
    }

    @Test
    public void testMotifExpression() {
        List<Token> tokens = new Lexer("motif(fork)").tokenize();
        assertThat(tokens).hasSize(5); // MOTIF, LPAREN, IDENTIFIER, RPAREN, EOF
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.MOTIF);
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.LPAREN);
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.IDENTIFIER);
        assertThat(tokens.get(2).value()).isEqualTo("fork");
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.RPAREN);
    }

    @Test
    public void testKeywords() {
        List<Token> tokens = new Lexer("AND OR NOT IN motif").tokenize();
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.AND);
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.OR);
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.NOT);
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.IN);
        assertThat(tokens.get(4).type()).isEqualTo(TokenType.MOTIF);
    }

    @Test
    public void testStringLiteral() {
        List<Token> tokens = new Lexer("\"chess.com\"").tokenize();
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.STRING);
        assertThat(tokens.get(0).value()).isEqualTo("chess.com");
    }

    @Test
    public void testAllOperators() {
        List<Token> tokens = new Lexer("= != < <= > >=").tokenize();
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.EQ);
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.NEQ);
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.LT);
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.LTE);
        assertThat(tokens.get(4).type()).isEqualTo(TokenType.GT);
        assertThat(tokens.get(5).type()).isEqualTo(TokenType.GTE);
    }

    @Test
    public void testDottedField() {
        List<Token> tokens = new Lexer("white.elo").tokenize();
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.IDENTIFIER);
        assertThat(tokens.get(0).value()).isEqualTo("white");
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.DOT);
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.IDENTIFIER);
        assertThat(tokens.get(2).value()).isEqualTo("elo");
    }

    @Test
    public void testInWithBrackets() {
        List<Token> tokens = new Lexer("platform IN [\"a\", \"b\"]").tokenize();
        assertThat(tokens.get(0).type()).isEqualTo(TokenType.IDENTIFIER);
        assertThat(tokens.get(1).type()).isEqualTo(TokenType.IN);
        assertThat(tokens.get(2).type()).isEqualTo(TokenType.LBRACKET);
        assertThat(tokens.get(3).type()).isEqualTo(TokenType.STRING);
        assertThat(tokens.get(4).type()).isEqualTo(TokenType.COMMA);
        assertThat(tokens.get(5).type()).isEqualTo(TokenType.STRING);
        assertThat(tokens.get(6).type()).isEqualTo(TokenType.RBRACKET);
    }

    @Test
    public void testUnterminatedString() {
        assertThatThrownBy(() -> new Lexer("\"unterminated").tokenize())
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessageContaining("Unterminated string");
    }
}
