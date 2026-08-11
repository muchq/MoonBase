package com.muchq.games.mcpserver.resources;

import io.micronaut.mcp.annotations.Resource;
import jakarta.inject.Singleton;
import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;

/**
 * Serves CHESSQL.md over MCP so a client can learn the query language before writing a query.
 *
 * <p>A resource rather than an eleventh tool, because this is context a client attaches up front,
 * not a call a model decides to make: spending a {@code tools/call} round trip to learn syntax is
 * the wrong shape, and the tool roster should stay a list of things that <em>do</em> something.
 *
 * <p>What {@code query_chess_games}' description cannot carry is the reason this exists. That
 * paragraph lists the vocabulary — fields, perspective fields, motifs — and stops there. The
 * grammar, operator precedence ({@code a OR b AND c} parses as {@code a OR (b AND c)}), which
 * fields are filter-only, that underscore spellings are equivalent, and the NULL semantics are all
 * only in the doc. Without it an assistant is inferring syntax from three examples.
 *
 * <p>Served verbatim. The field and motif rosters in it duplicate what {@code SqlCompiler} accepts,
 * which is a third copy and would drift like any other — {@code ChessQlReferenceTest} in one_d4 is
 * what stops it, by failing when the doc's tables and the compiler's maps disagree.
 */
@Singleton
public class ChessQlReferenceResource {

  static final String URI = "chessql://reference";

  /**
   * Classpath root, from {@code //domains/games/apis/one_d4:chessql_reference} stripping the docs
   * directory prefix. The file is not in this package, so it cannot be loaded relative to this
   * class.
   */
  private static final String CLASSPATH_PATH = "/CHESSQL.md";

  private final String reference = load();

  /**
   * Returning {@code String} is load-bearing. micronaut-mcp wraps a String in {@code
   * TextResourceContents} using this annotation's {@code mimeType}; a method returning anything
   * else it does not recognise yields a {@code ReadResourceResult} with <em>no</em> contents and no
   * error, so the resource would list fine and read empty. {@code McpResourceContractTest} pins the
   * return type for that reason, the same way {@code McpToolRosterContractTest} pins tools'.
   */
  @Resource(
      uri = URI,
      name = "chessql_reference",
      title = "ChessQL reference",
      description =
          "The ChessQL query language reference: grammar (EBNF), operator precedence, the full"
              + " field and motif rosters, perspective fields, date scoping and NULL semantics."
              + " Read this before writing a query for query_chess_games or the filter argument"
              + " of aggregate_chess_games.",
      mimeType = "text/markdown")
  public String chessQlReference() {
    return reference;
  }

  private static String load() {
    try (InputStream in = ChessQlReferenceResource.class.getResourceAsStream(CLASSPATH_PATH)) {
      if (in == null) {
        // Loud, because the alternative is serving an empty reference that reads as "ChessQL has
        // no documentation" rather than as a packaging bug.
        throw new IllegalStateException(
            CLASSPATH_PATH
                + " is not on the classpath — the mcpserver binary is missing"
                + " //domains/games/apis/one_d4:chessql_reference");
      }
      return new String(in.readAllBytes(), StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new UncheckedIOException("could not read " + CLASSPATH_PATH, e);
    }
  }
}
