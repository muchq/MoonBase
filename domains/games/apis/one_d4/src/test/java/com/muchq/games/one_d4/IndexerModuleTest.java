package com.muchq.games.one_d4;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

public class IndexerModuleTest {

  @TempDir Path tmp;

  @Test
  public void readJdbcUrl_returnsEnvVar_whenSet() {
    String result = IndexerModule.readJdbcUrl("jdbc:postgresql://prod:5432/db", missingPath());
    assertThat(result).isEqualTo("jdbc:postgresql://prod:5432/db");
  }

  @Test
  public void readJdbcUrl_stripsEnvVar() {
    String result = IndexerModule.readJdbcUrl("  jdbc:postgresql://host/db  ", missingPath());
    assertThat(result).isEqualTo("jdbc:postgresql://host/db");
  }

  @Test
  public void readJdbcUrl_ignoresBlankEnvVar() {
    String result = IndexerModule.readJdbcUrl("   ", missingPath());
    assertThat(result).isEqualTo("jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1");
  }

  @Test
  public void readJdbcUrl_ignoresNullEnvVar_andReadsFile() throws IOException {
    Path configFile = Files.createFile(tmp.resolve("db_config"));
    Files.writeString(configFile, "jdbc:postgresql://file-host:5432/chess\n");

    String result = IndexerModule.readJdbcUrl(null, configFile);
    assertThat(result).isEqualTo("jdbc:postgresql://file-host:5432/chess");
  }

  @Test
  public void readJdbcUrl_returnsDefault_whenFileIsMissing() {
    String result = IndexerModule.readJdbcUrl(null, missingPath());
    assertThat(result).isEqualTo("jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1");
  }

  @Test
  public void readJdbcUrl_returnsDefault_whenFileIsEmpty() throws IOException {
    Path configFile = Files.createFile(tmp.resolve("db_config_empty"));
    Files.writeString(configFile, "   ");

    String result = IndexerModule.readJdbcUrl(null, configFile);
    assertThat(result).isEqualTo("jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1");
  }

  @Test
  public void readJdbcUrl_envVarTakesPrecedenceOverFile() throws IOException {
    Path configFile = Files.createFile(tmp.resolve("db_config_precedence"));
    Files.writeString(configFile, "jdbc:postgresql://file-host/db");

    String result = IndexerModule.readJdbcUrl("jdbc:postgresql://env-host/db", configFile);
    assertThat(result).isEqualTo("jdbc:postgresql://env-host/db");
  }

  @Test
  public void readJdbcUrl_throwsUncheckedIOException_onNonFileNotFoundIOError() {
    // A directory path will cause an IOException (not NoSuchFileException) when read as a file
    Path dirPath = tmp;

    assertThatThrownBy(() -> IndexerModule.readJdbcUrl(null, dirPath))
        .isInstanceOf(UncheckedIOException.class)
        .hasCauseInstanceOf(IOException.class);
  }

  @Test
  public void parseThreads_returnsDefault_whenNull() {
    assertThat(IndexerModule.parseThreads(null, 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenBlank() {
    assertThat(IndexerModule.parseThreads("   ", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenUnparseable() {
    assertThat(IndexerModule.parseThreads("abc", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenNonPositive() {
    assertThat(IndexerModule.parseThreads("0", 4)).isEqualTo(4);
    assertThat(IndexerModule.parseThreads("-3", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_respectsValidValue() {
    assertThat(IndexerModule.parseThreads("8", 4)).isEqualTo(8);
    assertThat(IndexerModule.parseThreads(" 16 ", 4)).isEqualTo(16);
  }

  private Path missingPath() {
    return tmp.resolve("nonexistent_db_config");
  }
}
