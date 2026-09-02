package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.ast.AndExpr;
import com.muchq.games.chessql.ast.ComparisonExpr;
import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.ast.InExpr;
import com.muchq.games.chessql.ast.MotifExpr;
import com.muchq.games.chessql.ast.NotExpr;
import com.muchq.games.chessql.ast.OrExpr;
import com.muchq.games.chessql.ast.SequenceExpr;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.ParsedQuery;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

/**
 * The outline of a query with the caller's data removed: which fields it filters on, which motifs
 * it asks for, and the motif it orders by. Only names the compiler knows survive — the parser
 * accepts any identifier, and the compiler is what rejects {@code motif(zzz)}, so a shape taken
 * before compilation is bounded here rather than there. Underscore spellings collapse to the dotted
 * canonical form. Values (usernames, ratings, dates) never appear. This is what the query event
 * logs (#1465): enough to say which parts of ChessQL get used, and nothing that identifies who
 * asked.
 */
record QueryShape(List<String> fields, List<String> motifs, String orderBy) {

  private static final Set<String> KNOWN_FIELDS = knownFields();
  private static final Set<String> KNOWN_MOTIFS = SqlCompiler.motifs();

  private static Set<String> knownFields() {
    Set<String> fields = new HashSet<>(SqlCompiler.filterableFields());
    fields.addAll(SqlCompiler.perspectiveFields());
    return Set.copyOf(fields);
  }

  static QueryShape of(ParsedQuery parsed) {
    Set<String> fields = new TreeSet<>();
    Set<String> motifs = new TreeSet<>();
    collect(parsed.expr(), fields, motifs);
    String orderBy = parsed.orderBy() == null ? "" : knownMotif(parsed.orderBy().motifName());
    return new QueryShape(List.copyOf(fields), List.copyOf(motifs), orderBy);
  }

  private static void collect(Expr expr, Set<String> fields, Set<String> motifs) {
    switch (expr) {
      case ComparisonExpr comparison -> addField(fields, comparison.field());
      case InExpr in -> addField(fields, in.field());
      case MotifExpr motif -> addMotif(motifs, motif.motifName());
      case SequenceExpr sequence -> sequence.motifNames().forEach(name -> addMotif(motifs, name));
      case NotExpr not -> collect(not.operand(), fields, motifs);
      case AndExpr and -> and.operands().forEach(operand -> collect(operand, fields, motifs));
      case OrExpr or -> or.operands().forEach(operand -> collect(operand, fields, motifs));
    }
  }

  private static void addField(Set<String> fields, String name) {
    if (KNOWN_FIELDS.contains(name)) {
      fields.add(name);
      return;
    }
    String dotted = name.replace('_', '.');
    if (KNOWN_FIELDS.contains(dotted)) {
      fields.add(dotted);
    }
  }

  private static void addMotif(Set<String> motifs, String name) {
    String known = knownMotif(name);
    if (!known.isEmpty()) {
      motifs.add(known);
    }
  }

  private static String knownMotif(String name) {
    return KNOWN_MOTIFS.contains(name) ? name : "";
  }
}
