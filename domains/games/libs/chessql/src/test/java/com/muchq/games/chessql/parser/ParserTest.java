package com.muchq.games.chessql.parser;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.ast.AndExpr;
import com.muchq.games.chessql.ast.ComparisonExpr;
import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.ast.InExpr;
import com.muchq.games.chessql.ast.MotifExpr;
import com.muchq.games.chessql.ast.NotExpr;
import com.muchq.games.chessql.ast.OrExpr;
import com.muchq.games.chessql.ast.OrderByClause;
import com.muchq.games.chessql.ast.SequenceExpr;
import java.util.List;
import org.junit.Test;

public class ParserTest {

  @Test
  public void testSimpleComparison() {
    Expr expr = Parser.parse("white_elo >= 2500").expr();
    assertThat(expr).isInstanceOf(ComparisonExpr.class);
    ComparisonExpr cmp = (ComparisonExpr) expr;
    assertThat(cmp.field()).isEqualTo("white_elo");
    assertThat(cmp.operator()).isEqualTo(">=");
    assertThat(cmp.value()).isEqualTo(2500);
  }

  @Test
  public void testDottedFieldComparison() {
    Expr expr = Parser.parse("white.elo >= 2500").expr();
    assertThat(expr).isInstanceOf(ComparisonExpr.class);
    ComparisonExpr cmp = (ComparisonExpr) expr;
    assertThat(cmp.field()).isEqualTo("white.elo");
    assertThat(cmp.operator()).isEqualTo(">=");
    assertThat(cmp.value()).isEqualTo(2500);
  }

  @Test
  public void testMotifExpression() {
    Expr expr = Parser.parse("motif(fork)").expr();
    assertThat(expr).isInstanceOf(MotifExpr.class);
    assertThat(((MotifExpr) expr).motifName()).isEqualTo("fork");
  }

  @Test
  public void testAndExpression() {
    Expr expr = Parser.parse("white.elo >= 2500 AND motif(cross_pin)").expr();
    assertThat(expr).isInstanceOf(AndExpr.class);
    AndExpr and = (AndExpr) expr;
    assertThat(and.operands()).hasSize(2);
    assertThat(and.operands().get(0)).isInstanceOf(ComparisonExpr.class);
    assertThat(and.operands().get(1)).isInstanceOf(MotifExpr.class);
  }

  @Test
  public void testOrExpression() {
    Expr expr = Parser.parse("motif(fork) OR motif(pin)").expr();
    assertThat(expr).isInstanceOf(OrExpr.class);
    OrExpr or = (OrExpr) expr;
    assertThat(or.operands()).hasSize(2);
  }

  @Test
  public void testNotExpression() {
    Expr expr = Parser.parse("NOT motif(pin)").expr();
    assertThat(expr).isInstanceOf(NotExpr.class);
    NotExpr not = (NotExpr) expr;
    assertThat(not.operand()).isInstanceOf(MotifExpr.class);
  }

  @Test
  public void testInExpression() {
    Expr expr = Parser.parse("platform IN [\"lichess\", \"chess.com\"]").expr();
    assertThat(expr).isInstanceOf(InExpr.class);
    InExpr in = (InExpr) expr;
    assertThat(in.field()).isEqualTo("platform");
    assertThat(in.values()).isEqualTo(List.of("lichess", "chess.com"));
  }

  @Test
  public void testComplexExpression() {
    Expr expr = Parser.parse("white.elo >= 2500 AND motif(fork) AND NOT motif(pin)").expr();
    assertThat(expr).isInstanceOf(AndExpr.class);
    AndExpr and = (AndExpr) expr;
    assertThat(and.operands()).hasSize(3);
    assertThat(and.operands().get(2)).isInstanceOf(NotExpr.class);
  }

  @Test
  public void testParenthesizedExpression() {
    Expr expr = Parser.parse("(motif(fork) OR motif(pin)) AND white.elo > 2000").expr();
    assertThat(expr).isInstanceOf(AndExpr.class);
    AndExpr and = (AndExpr) expr;
    assertThat(and.operands().get(0)).isInstanceOf(OrExpr.class);
    assertThat(and.operands().get(1)).isInstanceOf(ComparisonExpr.class);
  }

  @Test
  public void testPrecedence() {
    // AND binds tighter than OR
    Expr expr = Parser.parse("motif(fork) OR motif(pin) AND white.elo > 2000").expr();
    assertThat(expr).isInstanceOf(OrExpr.class);
    OrExpr or = (OrExpr) expr;
    assertThat(or.operands()).hasSize(2);
    assertThat(or.operands().get(0)).isInstanceOf(MotifExpr.class);
    assertThat(or.operands().get(1)).isInstanceOf(AndExpr.class);
  }

  @Test
  public void testStringComparison() {
    Expr expr = Parser.parse("eco = \"B90\"").expr();
    assertThat(expr).isInstanceOf(ComparisonExpr.class);
    ComparisonExpr cmp = (ComparisonExpr) expr;
    assertThat(cmp.field()).isEqualTo("eco");
    assertThat(cmp.value()).isEqualTo("B90");
  }

  @Test
  public void testParseError() {
    assertThatThrownBy(() -> Parser.parse("AND")).isInstanceOf(ParseException.class);
  }

  @Test
  public void testSequenceExpression() {
    ParsedQuery parsed = Parser.parse("sequence(fork THEN check THEN checkmate)");
    assertThat(parsed.orderBy()).isNull();
    Expr expr = parsed.expr();
    assertThat(expr).isInstanceOf(SequenceExpr.class);
    SequenceExpr seq = (SequenceExpr) expr;
    assertThat(seq.motifNames()).containsExactly("fork", "check", "checkmate");
  }

  @Test
  public void testSequenceTwoMotifs() {
    ParsedQuery parsed = Parser.parse("sequence(pin THEN skewer)");
    assertThat(parsed.expr()).isInstanceOf(SequenceExpr.class);
    assertThat(((SequenceExpr) parsed.expr()).motifNames()).containsExactly("pin", "skewer");
  }

  @Test
  public void testOrderByClause() {
    ParsedQuery parsed = Parser.parse("motif(fork) ORDER BY motif_count(checkmate) DESC");
    assertThat(parsed.orderBy()).isNotNull();
    OrderByClause orderBy = parsed.orderBy();
    assertThat(orderBy.motifName()).isEqualTo("checkmate");
    assertThat(orderBy.ascending()).isFalse();
  }

  @Test
  public void testOrderByClauseAsc() {
    ParsedQuery parsed = Parser.parse("white.elo >= 2500 ORDER BY motif_count(pin) ASC");
    assertThat(parsed.orderBy()).isNotNull();
    OrderByClause orderBy = parsed.orderBy();
    assertThat(orderBy.motifName()).isEqualTo("pin");
    assertThat(orderBy.ascending()).isTrue();
  }
}
