package com.muchq.games.chessql.compiler;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.parser.Parser;
import java.util.List;
import org.junit.Test;

public class SqlCompilerTest {

  private final SqlCompiler compiler = new SqlCompiler();

  @Test
  public void testSimpleComparison() {
    CompiledQuery result = compile("white.elo >= 2500");
    assertThat(result.sql()).isEqualTo("white_elo >= ?");
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testMotif() {
    CompiledQuery result = compile("motif(fork)");
    assertThat(result.sql()).isEqualTo("has_fork = TRUE");
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testAndExpression() {
    CompiledQuery result = compile("white.elo >= 2500 AND motif(fork)");
    assertThat(result.sql()).isEqualTo("(white_elo >= ? AND has_fork = TRUE)");
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testOrExpression() {
    CompiledQuery result = compile("motif(fork) OR motif(pin)");
    assertThat(result.sql()).isEqualTo("(has_fork = TRUE OR has_pin = TRUE)");
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testNotExpression() {
    CompiledQuery result = compile("NOT motif(pin)");
    assertThat(result.sql()).isEqualTo("(NOT has_pin = TRUE)");
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testInExpression() {
    CompiledQuery result = compile("platform IN [\"lichess\", \"chess.com\"]");
    assertThat(result.sql()).isEqualTo("platform IN (?, ?)");
    assertThat(result.parameters()).isEqualTo(List.of("lichess", "chess.com"));
  }

  @Test
  public void testComplexQuery() {
    CompiledQuery result = compile("white.elo >= 2500 AND motif(cross_pin)");
    assertThat(result.sql()).isEqualTo("(white_elo >= ? AND has_cross_pin = TRUE)");
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testEndToEnd() {
    CompiledQuery result = compile("white.elo >= 2500 AND motif(fork)");
    assertThat(result.sql()).isEqualTo("(white_elo >= ? AND has_fork = TRUE)");
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testNestedBooleans() {
    CompiledQuery result = compile("(motif(fork) OR motif(pin)) AND white.elo > 2000");
    assertThat(result.sql()).isEqualTo("((has_fork = TRUE OR has_pin = TRUE) AND white_elo > ?)");
    assertThat(result.parameters()).isEqualTo(List.of(2000));
  }

  @Test
  public void testUnknownMotif() {
    assertThatThrownBy(() -> compile("motif(unknown)"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown motif");
  }

  @Test
  public void testUnknownField() {
    assertThatThrownBy(() -> compile("bogus_field >= 100"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown field");
  }

  @Test
  public void testDirectColumnName() {
    CompiledQuery result = compile("white_elo >= 2500");
    assertThat(result.sql()).isEqualTo("white_elo >= ?");
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  private CompiledQuery compile(String input) {
    Expr expr = Parser.parse(input);
    return compiler.compile(expr);
  }
}
