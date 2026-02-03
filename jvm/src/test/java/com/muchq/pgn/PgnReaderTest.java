package com.muchq.pgn;

import com.muchq.pgn.model.GameResult;
import com.muchq.pgn.model.PgnGame;
import org.junit.Test;

import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;

public class PgnReaderTest {

    @Test
    public void parseGame_minimalGame() {
        PgnGame game = PgnReader.parseGame("[Result \"*\"] *");
        assertThat(game.result()).isEqualTo(GameResult.ONGOING);
        assertThat(game.moves()).isEmpty();
    }

    @Test
    public void parseGame_simpleGame() {
        String pgn = """
            [Event "Test"]
            [Result "1-0"]

            1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0
            """;
        PgnGame game = PgnReader.parseGame(pgn);

        assertThat(game.getTag("Event")).hasValue("Test");
        assertThat(game.moves()).hasSize(7);
        assertThat(game.result()).isEqualTo(GameResult.WHITE_WINS);
    }

    @Test
    public void parseGame_withAllAnnotations() {
        String pgn = """
            [Event "Annotated Game"]
            [Result "*"]

            1. e4 $1 {The king's pawn opening} e5
            2. Nf3 (2. f4 {King's Gambit}) Nc6 $2
            3. Bb5 {Ruy Lopez} *
            """;
        PgnGame game = PgnReader.parseGame(pgn);

        assertThat(game.moves()).hasSize(5);
        assertThat(game.moves().get(0).nags()).isNotEmpty();
        assertThat(game.moves().get(0).comment()).isPresent();
        assertThat(game.moves().get(2).variations()).hasSize(1);
    }

    @Test
    public void parseAll_multipleGames() {
        String pgn = """
            [Event "Game 1"]
            [Result "1-0"]
            1. e4 1-0

            [Event "Game 2"]
            [Result "0-1"]
            1. d4 0-1

            [Event "Game 3"]
            [Result "1/2-1/2"]
            1. c4 1/2-1/2
            """;
        List<PgnGame> games = PgnReader.parseAll(pgn);

        assertThat(games).hasSize(3);
        assertThat(games.get(0).getTag("Event")).hasValue("Game 1");
        assertThat(games.get(0).moves().get(0).san()).isEqualTo("e4");
        assertThat(games.get(1).getTag("Event")).hasValue("Game 2");
        assertThat(games.get(1).moves().get(0).san()).isEqualTo("d4");
        assertThat(games.get(2).getTag("Event")).hasValue("Game 3");
        assertThat(games.get(2).moves().get(0).san()).isEqualTo("c4");
    }

    @Test
    public void parseAll_empty() {
        List<PgnGame> games = PgnReader.parseAll("");
        assertThat(games).isEmpty();
    }

    @Test
    public void parseGame_realWorldPgn_fischerSpassky() {
        String pgn = """
            [Event "F/S Return Match"]
            [Site "Belgrade, Serbia JUG"]
            [Date "1992.11.04"]
            [Round "29"]
            [White "Fischer, Robert J."]
            [Black "Spassky, Boris V."]
            [Result "1/2-1/2"]

            1. e4 e5 2. Nf3 Nc6 3. Bb5 {This opening is called the Ruy Lopez.}
            a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O 9. h3 Nb8 10. d4 Nbd7
            11. c4 c6 12. cxb5 axb5 13. Nc3 Bb7 14. Bg5 b4 15. Nb1 h6 16. Bh4 c5 17. dxe5
            Nxe4 18. Bxe7 Qxe7 19. exd6 Qf6 20. Nbd2 Nxd6 21. Nc4 Nxc4 22. Bxc4 Nb6
            23. Ne5 Rae8 24. Bxf7+ Rxf7 25. Nxf7 Rxe1+ 26. Qxe1 Kxf7 27. Qe3 Qg5 28. Qxg5
            hxg5 29. b3 Ke6 30. a3 Kd6 31. axb4 cxb4 32. Ra5 Nd5 33. f3 Bc8 34. Kf2 Bf5
            35. Ra7 g6 36. Ra6+ Kc5 37. Ke1 Nf4 38. g3 Nxh3 39. Kd2 Kb5 40. Rd6 Kc5 41. Ra6
            Nf2 42. g4 Bd3 43. Re6 1/2-1/2
            """;
        PgnGame game = PgnReader.parseGame(pgn);

        assertThat(game.getTag("Event")).hasValue("F/S Return Match");
        assertThat(game.getTag("White")).hasValue("Fischer, Robert J.");
        assertThat(game.getTag("Black")).hasValue("Spassky, Boris V.");
        assertThat(game.result()).isEqualTo(GameResult.DRAW);
        assertThat(game.moves()).hasSizeGreaterThan(80);
    }

    @Test
    public void parseGame_deeplyNestedVariations() {
        String pgn = """
            [Result "*"]

            1. e4 (1. d4 (1. c4 (1. Nf3))) *
            """;
        PgnGame game = PgnReader.parseGame(pgn);

        assertThat(game.moves()).hasSize(1);
        assertThat(game.moves().get(0).san()).isEqualTo("e4");

        // First level variation
        assertThat(game.moves().get(0).variations()).hasSize(1);
        var d4 = game.moves().get(0).variations().get(0).get(0);
        assertThat(d4.san()).isEqualTo("d4");

        // Second level variation
        assertThat(d4.variations()).hasSize(1);
        var c4 = d4.variations().get(0).get(0);
        assertThat(c4.san()).isEqualTo("c4");

        // Third level variation
        assertThat(c4.variations()).hasSize(1);
        var nf3 = c4.variations().get(0).get(0);
        assertThat(nf3.san()).isEqualTo("Nf3");
    }

    @Test
    public void parseGame_longVariation() {
        String pgn = """
            [Result "*"]

            1. e4 c5 (1... e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O) 2. Nf3 *
            """;
        PgnGame game = PgnReader.parseGame(pgn);

        assertThat(game.moves()).hasSize(3); // e4, c5, Nf3 in main line
        var c5 = game.moves().get(1);
        assertThat(c5.variations()).hasSize(1);
        assertThat(c5.variations().get(0)).hasSize(9); // e5, Nf3, Nc6, Bb5, a6, Ba4, Nf6, O-O
    }
}
