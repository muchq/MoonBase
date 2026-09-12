package com.muchq.games.one_d4.db;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Locates and reads the numbered migration files (#1419): classpath resources under {@code
 * one_d4/migrations}, ordered by {@code manifest.txt}. The files are the one copy of the DDL —
 * {@code migrations/README.md} is the authoring contract — and this class is deliberately dumb
 * about their contents: ordering comes from the manifest, splitting from {@link SqlStatements}, and
 * everything else from the files themselves.
 */
public final class MigrationFiles {

  static final String ROOT = "one_d4/migrations";

  private MigrationFiles() {}

  /** The manifest's step names, in the order they run. */
  public static List<String> steps() {
    return steps(ROOT);
  }

  static List<String> steps(String root) {
    String manifest = read(root + "/manifest.txt", "manifest");
    List<String> steps = new ArrayList<>();
    for (String line : manifest.split("\n", -1)) {
      String step = line.strip();
      if (!step.isEmpty() && !step.startsWith("#")) {
        steps.add(step);
      }
    }
    return steps;
  }

  /** The SQL for one step. */
  public static String sqlFor(String step) {
    return sqlFor(ROOT, step);
  }

  static String sqlFor(String root, String step) {
    return read(root + "/" + step + ".sql", "migration step " + step);
  }

  private static String read(String path, String what) {
    try (InputStream in = MigrationFiles.class.getClassLoader().getResourceAsStream(path)) {
      if (in == null) {
        throw new IllegalStateException("no " + what + " resource at " + path);
      }
      return new String(in.readAllBytes(), StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new IllegalStateException("failed reading " + what + " resource at " + path, e);
    }
  }
}
