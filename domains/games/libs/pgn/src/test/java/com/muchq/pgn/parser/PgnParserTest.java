package com.muchq.pgn.parser;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.pgn.lexer.PgnLexer;
import com.muchq.pgn.lexer.Token;
import com.muchq.pgn.model.GameResult;
import com.muchq.pgn.model.Move;
import com.muchq.pgn.model.PgnGame;
import java.util.List;
import org.junit.Test;

public class PgnParserTest {

  private PgnGame parse(String pgn) {
    List<Token> tokens = new PgnLexer(pgn).tokenize();
    return new PgnParser(tokens).parseGame();
  }

  private List<PgnGame> parseAll(String pgn) {
    List<Token> tokens = new PgnLexer(pgn).tokenize();
    return new PgnParser(tokens).parseAll();
  }

  // === Tag Parsing Tests ===

  @Test
  public void parse_singleTag() {
    PgnGame game = parse("[Event \"Test\"] *");
    assertThat(game.tags()).hasSize(1);
    assertThat(game.tags().get(0).name()).isEqualTo("Event");
    assertThat(game.tags().get(0).value()).isEqualTo("Test");
  }

  @Test
  public void parse_sevenTagRoster() {
    String pgn =
        """
        [Event "F/S Return Match"]
        [Site "Belgrade, Serbia JUG"]
        [Date "1992.11.04"]
        [Round "29"]
        [White "Fischer, Robert J."]
        [Black "Spassky, Boris V."]
        [Result "1/2-1/2"]

        1/2-1/2
        """;
    PgnGame game = parse(pgn);
    assertThat(game.tags()).hasSize(7);
    assertThat(game.getTag("Event")).hasValue("F/S Return Match");
    assertThat(game.getTag("Site")).hasValue("Belgrade, Serbia JUG");
    assertThat(game.getTag("Date")).hasValue("1992.11.04");
    assertThat(game.getTag("Round")).hasValue("29");
    assertThat(game.getTag("White")).hasValue("Fischer, Robert J.");
    assertThat(game.getTag("Black")).hasValue("Spassky, Boris V.");
    assertThat(game.getTag("Result")).hasValue("1/2-1/2");
  }

  @Test
  public void parse_tagWithSpecialCharacters() {
    PgnGame game = parse("[White \"O'Brien, John\"] *");
    assertThat(game.getTag("White")).hasValue("O'Brien, John");
  }

  @Test
  public void parse_tagWithEscapedQuote() {
    PgnGame game = parse("[Event \"The \\\"Big\\\" Game\"] *");
    assertThat(game.getTag("Event")).hasValue("The \"Big\" Game");
  }

  // === Simple Movetext Tests ===

  @Test
  public void parse_singleMove() {
    PgnGame game = parse("[Result \"*\"] 1. e4 *");
    assertThat(game.moves()).hasSize(1);
    assertThat(game.moves().get(0).san()).isEqualTo("e4");
  }

  @Test
  public void parse_twoMoves() {
    PgnGame game = parse("[Result \"*\"] 1. e4 e5 *");
    assertThat(game.moves()).hasSize(2);
    assertThat(game.moves().get(0).san()).isEqualTo("e4");
    assertThat(game.moves().get(1).san()).isEqualTo("e5");
  }

  @Test
  public void parse_multipleMoveNumbers() {
    PgnGame game = parse("[Result \"*\"] 1. e4 e5 2. Nf3 Nc6 3. Bb5 *");
    assertThat(game.moves()).hasSize(5);
    assertThat(game.moves().get(0).san()).isEqualTo("e4");
    assertThat(game.moves().get(1).san()).isEqualTo("e5");
    assertThat(game.moves().get(2).san()).isEqualTo("Nf3");
    assertThat(game.moves().get(3).san()).isEqualTo("Nc6");
    assertThat(game.moves().get(4).san()).isEqualTo("Bb5");
  }

  @Test
  public void parse_blackToMove() {
    // Continuation notation: 15... Qxd4
    PgnGame game = parse("[Result \"*\"] 15... Qxd4 *");
    assertThat(game.moves()).hasSize(1);
    assertThat(game.moves().get(0).san()).isEqualTo("Qxd4");
  }

  // === Castling Tests ===

  @Test
  public void parse_castleKingside() {
    PgnGame game = parse("[Result \"*\"] 1. O-O *");
    assertThat(game.moves().get(0).san()).isEqualTo("O-O");
  }

  @Test
  public void parse_castleQueenside() {
    PgnGame game = parse("[Result \"*\"] 1. O-O-O *");
    assertThat(game.moves().get(0).san()).isEqualTo("O-O-O");
  }

  // === Check and Checkmate Tests ===

  @Test
  public void parse_check() {
    PgnGame game = parse("[Result \"*\"] 1. Qh5+ *");
    assertThat(game.moves().get(0).san()).isEqualTo("Qh5+");
  }

  @Test
  public void parse_checkmate() {
    PgnGame game = parse("[Result \"1-0\"] 1. Qxf7# 1-0");
    assertThat(game.moves().get(0).san()).isEqualTo("Qxf7#");
  }

  // === Promotion Tests ===

  @Test
  public void parse_promotion() {
    PgnGame game = parse("[Result \"*\"] 1. e8=Q *");
    assertThat(game.moves().get(0).san()).isEqualTo("e8=Q");
  }

  @Test
  public void parse_promotionWithCheck() {
    PgnGame game = parse("[Result \"*\"] 1. e8=Q+ *");
    assertThat(game.moves().get(0).san()).isEqualTo("e8=Q+");
  }

  // === Capture Tests ===

  @Test
  public void parse_pieceCapture() {
    PgnGame game = parse("[Result \"*\"] 1. Bxe5 *");
    assertThat(game.moves().get(0).san()).isEqualTo("Bxe5");
  }

  @Test
  public void parse_pawnCapture() {
    PgnGame game = parse("[Result \"*\"] 1. exd5 *");
    assertThat(game.moves().get(0).san()).isEqualTo("exd5");
  }

  // === Disambiguation Tests ===

  @Test
  public void parse_disambiguatedByFile() {
    PgnGame game = parse("[Result \"*\"] 1. Rae1 *");
    assertThat(game.moves().get(0).san()).isEqualTo("Rae1");
  }

  @Test
  public void parse_disambiguatedByRank() {
    PgnGame game = parse("[Result \"*\"] 1. R1e4 *");
    assertThat(game.moves().get(0).san()).isEqualTo("R1e4");
  }

  @Test
  public void parse_fullyDisambiguated() {
    PgnGame game = parse("[Result \"*\"] 1. Qd1e2 *");
    assertThat(game.moves().get(0).san()).isEqualTo("Qd1e2");
  }

  // === Comment Tests ===

  @Test
  public void parse_moveWithComment() {
    PgnGame game = parse("[Result \"*\"] 1. e4 {King's pawn opening} *");
    assertThat(game.moves()).hasSize(1);
    assertThat(game.moves().get(0).san()).isEqualTo("e4");
    assertThat(game.moves().get(0).comment()).hasValue("King's pawn opening");
  }

  @Test
  public void parse_multipleCommentsAttachToMove() {
    // Multiple comments after a move - they should be concatenated or only first kept
    PgnGame game = parse("[Result \"*\"] 1. e4 {comment one} {comment two} *");
    assertThat(game.moves()).hasSize(1);
    assertThat(game.moves().get(0).comment()).isPresent();
  }

  @Test
  public void parse_commentBeforeMove() {
    // Comment before moves is valid PGN
    PgnGame game = parse("[Result \"*\"] {Opening comment} 1. e4 *");
    assertThat(game.moves()).hasSize(1);
  }

  // === NAG Tests ===

  @Test
  public void parse_moveWithNag() {
    PgnGame game = parse("[Result \"*\"] 1. e4 $1 *");
    assertThat(game.moves()).hasSize(1);
    assertThat(game.moves().get(0).nags()).hasSize(1);
    assertThat(game.moves().get(0).nags().get(0).value()).isEqualTo(1);
  }

  @Test
  public void parse_moveWithMultipleNags() {
    PgnGame game = parse("[Result \"*\"] 1. e4 $1 $14 *");
    assertThat(game.moves().get(0).nags()).hasSize(2);
    assertThat(game.moves().get(0).nags().get(0).value()).isEqualTo(1);
    assertThat(game.moves().get(0).nags().get(1).value()).isEqualTo(14);
  }

  @Test
  public void parse_inlineAnnotation_goodMove() {
    // ! should be converted to $1
    PgnGame game = parse("[Result \"*\"] 1. e4! *");
    assertThat(game.moves()).hasSize(1);
    // Either the ! is part of the SAN or converted to NAG
    Move move = game.moves().get(0);
    boolean hasGoodMoveIndicator =
        move.san().endsWith("!") || move.nags().stream().anyMatch(n -> n.value() == 1);
    assertThat(hasGoodMoveIndicator).isTrue();
  }

  @Test
  public void parse_inlineAnnotation_blunder() {
    // ?? should be converted to $4
    PgnGame game = parse("[Result \"*\"] 1. e4?? *");
    assertThat(game.moves()).hasSize(1);
    Move move = game.moves().get(0);
    boolean hasBlunderIndicator =
        move.san().endsWith("??") || move.nags().stream().anyMatch(n -> n.value() == 4);
    assertThat(hasBlunderIndicator).isTrue();
  }

  // === Variation Tests ===

  @Test
  public void parse_simpleVariation() {
    PgnGame game = parse("[Result \"*\"] 1. e4 (1. d4) e5 *");
    assertThat(game.moves()).hasSize(2); // e4 and e5 in main line
    assertThat(game.moves().get(0).san()).isEqualTo("e4");
    assertThat(game.moves().get(0).variations()).hasSize(1);
    assertThat(game.moves().get(0).variations().get(0)).hasSize(1);
    assertThat(game.moves().get(0).variations().get(0).get(0).san()).isEqualTo("d4");
  }

  @Test
  public void parse_variationWithMultipleMoves() {
    PgnGame game = parse("[Result \"*\"] 1. e4 (1. d4 d5 2. c4) e5 *");
    assertThat(game.moves().get(0).variations()).hasSize(1);
    List<Move> variation = game.moves().get(0).variations().get(0);
    assertThat(variation).hasSize(3);
    assertThat(variation.get(0).san()).isEqualTo("d4");
    assertThat(variation.get(1).san()).isEqualTo("d5");
    assertThat(variation.get(2).san()).isEqualTo("c4");
  }

  @Test
  public void parse_nestedVariation() {
    PgnGame game = parse("[Result \"*\"] 1. e4 (1. d4 (1. c4)) e5 *");
    assertThat(game.moves().get(0).variations()).hasSize(1);
    List<Move> variation = game.moves().get(0).variations().get(0);
    assertThat(variation.get(0).san()).isEqualTo("d4");
    assertThat(variation.get(0).variations()).hasSize(1);
    assertThat(variation.get(0).variations().get(0).get(0).san()).isEqualTo("c4");
  }

  @Test
  public void parse_multipleVariations() {
    PgnGame game = parse("[Result \"*\"] 1. e4 (1. d4) (1. c4) e5 *");
    assertThat(game.moves().get(0).variations()).hasSize(2);
    assertThat(game.moves().get(0).variations().get(0).get(0).san()).isEqualTo("d4");
    assertThat(game.moves().get(0).variations().get(1).get(0).san()).isEqualTo("c4");
  }

  @Test
  public void parse_variationWithComment() {
    PgnGame game = parse("[Result \"*\"] 1. e4 (1. d4 {Queen's pawn}) *");
    List<Move> variation = game.moves().get(0).variations().get(0);
    assertThat(variation.get(0).comment()).hasValue("Queen's pawn");
  }

  // === Result Tests ===

  @Test
  public void parse_resultWhiteWins() {
    PgnGame game = parse("[Result \"1-0\"] 1. e4 1-0");
    assertThat(game.result()).isEqualTo(GameResult.WHITE_WINS);
  }

  @Test
  public void parse_resultBlackWins() {
    PgnGame game = parse("[Result \"0-1\"] 1. e4 0-1");
    assertThat(game.result()).isEqualTo(GameResult.BLACK_WINS);
  }

  @Test
  public void parse_resultDraw() {
    PgnGame game = parse("[Result \"1/2-1/2\"] 1. e4 1/2-1/2");
    assertThat(game.result()).isEqualTo(GameResult.DRAW);
  }

  @Test
  public void parse_resultOngoing() {
    PgnGame game = parse("[Result \"*\"] 1. e4 *");
    assertThat(game.result()).isEqualTo(GameResult.ONGOING);
  }

  // === Multiple Games Tests ===

  @Test
  public void parseAll_twoGames() {
    String pgn =
        """
        [Event "Game 1"]
        [Result "1-0"]

        1. e4 1-0

        [Event "Game 2"]
        [Result "0-1"]

        1. d4 0-1
        """;
    List<PgnGame> games = parseAll(pgn);
    assertThat(games).hasSize(2);
    assertThat(games.get(0).getTag("Event")).hasValue("Game 1");
    assertThat(games.get(0).result()).isEqualTo(GameResult.WHITE_WINS);
    assertThat(games.get(1).getTag("Event")).hasValue("Game 2");
    assertThat(games.get(1).result()).isEqualTo(GameResult.BLACK_WINS);
  }

  @Test
  public void parseAll_empty() {
    List<PgnGame> games = parseAll("");
    assertThat(games).isEmpty();
  }

  // === Complete Game Tests ===

  @Test
  public void parse_completeGame() {
    String pgn =
        """
        [Event "World Championship"]
        [Site "London"]
        [Date "2023.04.15"]
        [Round "5"]
        [White "Carlsen"]
        [Black "Nepomniachtchi"]
        [Result "1-0"]

        1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O 1-0
        """;
    PgnGame game = parse(pgn);

    assertThat(game.getTag("Event")).hasValue("World Championship");
    assertThat(game.getTag("White")).hasValue("Carlsen");
    assertThat(game.moves()).hasSize(9);
    assertThat(game.result()).isEqualTo(GameResult.WHITE_WINS);
  }

  @Test
  public void parse_gameWithAnnotations() {
    String pgn =
        """
        [Event "Test"]
        [Result "1-0"]

        1. e4 $1 {Best by test} e5 2. Nf3 Nc6 (2... d6 {Philidor}) 3. Bb5 1-0
        """;
    PgnGame game = parse(pgn);

    // Check first move has NAG and comment
    Move e4 = game.moves().get(0);
    assertThat(e4.san()).isEqualTo("e4");
    assertThat(e4.nags()).isNotEmpty();
    assertThat(e4.comment()).isPresent();

    // Check variation exists
    Move nc6 = game.moves().get(3);
    assertThat(nc6.san()).isEqualTo("Nc6");
    assertThat(nc6.variations()).hasSize(1);
  }

  // === Error Handling Tests ===

  @Test
  public void parse_missingResult() {
    // A game without a termination marker
    assertThatThrownBy(() -> parse("[Event \"Test\"] 1. e4")).isInstanceOf(ParseException.class);
  }

  @Test
  public void parse_unclosedVariation() {
    assertThatThrownBy(() -> parse("[Result \"*\"] 1. e4 (1. d4 *"))
        .isInstanceOf(ParseException.class);
  }

  @Test
  public void parse_malformedTag() {
    assertThatThrownBy(() -> parse("[Event] *")).isInstanceOf(ParseException.class);
  }

  // === Real World Examples ===

  @Test
  public void parse_operaGame() {
    String pgn =
        """
        [Event "Paris"]
        [Site "Paris FRA"]
        [Date "1858.??.??"]
        [Round "?"]
        [White "Morphy, Paul"]
        [Black "Duke of Brunswick and Count Isouard"]
        [Result "1-0"]

        1. e4 e5 2. Nf3 d6 3. d4 Bg4 4. dxe5 Bxf3 5. Qxf3 dxe5 6. Bc4 Nf6 7. Qb3 Qe7
        8. Nc3 c6 9. Bg5 b5 10. Nxb5 cxb5 11. Bxb5+ Nbd7 12. O-O-O Rd8
        13. Rxd7 Rxd7 14. Rd1 Qe6 15. Bxd7+ Nxd7 16. Qb8+ Nxb8 17. Rd8# 1-0
        """;
    PgnGame game = parse(pgn);

    assertThat(game.getTag("White")).hasValue("Morphy, Paul");
    assertThat(game.moves()).hasSize(33);
    assertThat(game.moves().get(32).san()).isEqualTo("Rd8#");
    assertThat(game.result()).isEqualTo(GameResult.WHITE_WINS);
  }

  @Test
  public void parse_immortalGame() {
    String pgn =
        """
        [Event "London"]
        [Site "London ENG"]
        [Date "1851.06.21"]
        [Round "?"]
        [White "Anderssen, Adolf"]
        [Black "Kieseritzky, Lionel"]
        [Result "1-0"]

        1. e4 e5 2. f4 exf4 3. Bc4 Qh4+ 4. Kf1 b5 5. Bxb5 Nf6 6. Nf3 Qh6 7. d3 Nh5
        8. Nh4 Qg5 9. Nf5 c6 10. g4 Nf6 11. Rg1 cxb5 12. h4 Qg6 13. h5 Qg5 14. Qf3 Ng8
        15. Bxf4 Qf6 16. Nc3 Bc5 17. Nd5 Qxb2 18. Bd6 Bxg1 19. e5 Qxa1+ 20. Ke2 Na6
        21. Nxg7+ Kd8 22. Qf6+ Nxf6 23. Be7# 1-0
        """;
    PgnGame game = parse(pgn);

    assertThat(game.getTag("Event")).hasValue("London");
    assertThat(game.moves()).hasSize(45);
    assertThat(game.moves().get(44).san()).isEqualTo("Be7#");
    assertThat(game.result()).isEqualTo(GameResult.WHITE_WINS);
  }
}
