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
 *
 * <p>A step resolves to {@code <engine>/<step>.sql} when the engines fork and {@code <step>.sql}
 * when they agree; exactly one of the two may exist. Both or neither is refused, not guessed around
 * — a migration that silently skipped a step, or picked one of two files claiming the same step,
 * would leave a schema nobody can reason about from the files.
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

  /** The SQL for one step on one engine ({@code "pg"} or {@code "h2"}). */
  public static String sqlFor(String step, String engine) {
    return sqlFor(ROOT, step, engine);
  }

  static String sqlFor(String root, String step, String engine) {
    String enginePath = root + "/" + engine + "/" + step + ".sql";
    String sharedPath = root + "/" + step + ".sql";
    boolean engineExists = exists(enginePath);
    boolean sharedExists = exists(sharedPath);
    if (engineExists && sharedExists) {
      throw new IllegalStateException(
          "migration step "
              + step
              + " has both "
              + enginePath
              + " and "
              + sharedPath
              + " on the classpath — a forked step must not also have a shared file");
    }
    if (!engineExists && !sharedExists) {
      throw new IllegalStateException(
          "migration step "
              + step
              + " has no SQL for engine "
              + engine
              + " — expected "
              + enginePath
              + " or "
              + sharedPath);
    }
    return read(engineExists ? enginePath : sharedPath, "migration step " + step);
  }

  private static boolean exists(String path) {
    return MigrationFiles.class.getClassLoader().getResource(path) != null;
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
