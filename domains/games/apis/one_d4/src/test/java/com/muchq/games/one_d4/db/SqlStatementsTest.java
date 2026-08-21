package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.List;
import org.junit.jupiter.api.Test;

public class SqlStatementsTest {

  @Test
  public void splitsOnTopLevelSemicolons() {
    assertThat(SqlStatements.split("CREATE TABLE a (x INT);\nCREATE TABLE b (y INT);\n"))
        .containsExactly("CREATE TABLE a (x INT)", "CREATE TABLE b (y INT)");
  }

  /**
   * The DO $$ ... END $$ blocks are why a splitter exists at all: they carry semicolons that are
   * body, not terminators, and a naive split hands Postgres a fragment ending mid-block.
   */
  @Test
  public void dollarQuotedBlockStaysOneStatement() {
    String block =
        """
        DO $$ BEGIN
          ALTER TABLE t ADD CONSTRAINT c UNIQUE (x);
        EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
        END $$;
        """;
    List<String> statements = SqlStatements.split(block);
    assertThat(statements).hasSize(1);
    assertThat(statements.get(0)).startsWith("DO $$").endsWith("END $$");
  }

  @Test
  public void taggedDollarQuoteStaysOneStatement() {
    assertThat(SqlStatements.split("SELECT $body$a;b$body$;"))
        .containsExactly("SELECT $body$a;b$body$");
  }

  @Test
  public void semicolonInsideSingleQuotesDoesNotSplit() {
    assertThat(SqlStatements.split("INSERT INTO t VALUES ('a;b');"))
        .containsExactly("INSERT INTO t VALUES ('a;b')");
  }

  @Test
  public void escapedQuoteInsideStringDoesNotEndIt() {
    assertThat(SqlStatements.split("INSERT INTO t VALUES ('it''s; fine');"))
        .containsExactly("INSERT INTO t VALUES ('it''s; fine')");
  }

  @Test
  public void semicolonInsideLineCommentDoesNotSplit() {
    assertThat(SqlStatements.split("-- not a terminator: ;\nSELECT 1;"))
        .containsExactly("-- not a terminator: ;\nSELECT 1");
  }

  @Test
  public void semicolonInsideBlockCommentDoesNotSplit() {
    assertThat(SqlStatements.split("/* a; b */ SELECT 1;")).containsExactly("/* a; b */ SELECT 1");
  }

  /** Postgres block comments nest; a splitter that closes on the first star-slash splits inside. */
  @Test
  public void nestedBlockCommentIsHonoured() {
    assertThat(SqlStatements.split("/* outer /* inner; */ still; */ SELECT 1;"))
        .containsExactly("/* outer /* inner; */ still; */ SELECT 1");
  }

  @Test
  public void commentOnlyTrailingChunkIsDropped() {
    assertThat(SqlStatements.split("SELECT 1;\n-- the end\n")).containsExactly("SELECT 1");
  }

  @Test
  public void whitespaceOnlyInputYieldsNothing() {
    assertThat(SqlStatements.split("  \n\t")).isEmpty();
    assertThat(SqlStatements.split("-- only a comment\n")).isEmpty();
  }

  /**
   * A final statement missing its terminator still runs. Dropping it silently would mean a
   * migration file whose last statement never executes anywhere while every test stays green.
   */
  @Test
  public void trailingStatementWithoutSemicolonIsKept() {
    assertThat(SqlStatements.split("SELECT 1;\nSELECT 2")).containsExactly("SELECT 1", "SELECT 2");
  }

  @Test
  public void unterminatedDollarQuoteIsAnError() {
    assertThatThrownBy(() -> SqlStatements.split("DO $$ BEGIN END"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("dollar-quote");
  }

  @Test
  public void unterminatedStringIsAnError() {
    assertThatThrownBy(() -> SqlStatements.split("SELECT 'oops"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("quote");
  }

  @Test
  public void unterminatedBlockCommentIsAnError() {
    assertThatThrownBy(() -> SqlStatements.split("SELECT 1 /* oops"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("comment");
  }

  /** A lone dollar sign is not an opener; it must not swallow the rest of the file. */
  @Test
  public void loneDollarSignIsPlainText() {
    assertThat(SqlStatements.split("SELECT '$'; SELECT 2;"))
        .containsExactly("SELECT '$'", "SELECT 2");
    assertThat(SqlStatements.split("SELECT a $ b; SELECT 2;"))
        .containsExactly("SELECT a $ b", "SELECT 2");
  }

  /**
   * A Postgres dollar-quote tag cannot begin with a digit — {@code $1$2} is two positional
   * parameters, and reading it as an opener would swallow to the next {@code $1$} or throw on a
   * file the server accepts.
   */
  @Test
  public void aDigitAfterTheDollarIsAParameterNotATag() {
    assertThat(SqlStatements.split("SELECT f($1$2); SELECT 2;"))
        .containsExactly("SELECT f($1$2)", "SELECT 2");
  }
}
