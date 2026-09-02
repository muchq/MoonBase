package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.ast.AndExpr;
import com.muchq.games.chessql.ast.ComparisonExpr;
import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.ast.InExpr;
import com.muchq.games.chessql.ast.MotifExpr;
import com.muchq.games.chessql.ast.NotExpr;
import com.muchq.games.chessql.ast.OrExpr;
import com.muchq.games.chessql.ast.SequenceExpr;
import com.muchq.games.chessql.parser.ParsedQuery;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

/**
 * The outline of a query with the caller's data removed: which fields it filters on, which motifs
 * it asks for, and the motif it orders by. Names come from the grammar, so the vocabulary is
 * bounded; values (usernames, ratings, dates) never appear. This is what the query event logs
 * (#1465) — enough to say which parts of ChessQL get used, and nothing that identifies who asked.
 */
record QueryShape(List<String> fields, List<String> motifs, String orderBy) {

  static QueryShape of(ParsedQuery parsed) {
    Set<String> fields = new TreeSet<>();
    Set<String> motifs = new TreeSet<>();
    collect(parsed.expr(), fields, motifs);
    String orderBy = parsed.orderBy() == null ? "" : parsed.orderBy().motifName();
    return new QueryShape(List.copyOf(fields), List.copyOf(motifs), orderBy);
  }

  private static void collect(Expr expr, Set<String> fields, Set<String> motifs) {
    switch (expr) {
      case ComparisonExpr comparison -> fields.add(comparison.field());
      case InExpr in -> fields.add(in.field());
      case MotifExpr motif -> motifs.add(motif.motifName());
      case SequenceExpr sequence -> motifs.addAll(sequence.motifNames());
      case NotExpr not -> collect(not.operand(), fields, motifs);
      case AndExpr and -> and.operands().forEach(operand -> collect(operand, fields, motifs));
      case OrExpr or -> or.operands().forEach(operand -> collect(operand, fields, motifs));
    }
  }
}
