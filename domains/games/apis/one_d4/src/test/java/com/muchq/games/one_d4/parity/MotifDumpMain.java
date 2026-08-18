package com.muchq.games.one_d4.parity;

import java.nio.file.Files;
import java.nio.file.Path;

/** Writes the parity golden: {@code bazel run //...:motif_dump -- <corpus.pgn> > golden.tsv}. */
public final class MotifDumpMain {

  private MotifDumpMain() {}

  public static void main(String[] args) throws Exception {
    if (args.length != 1) {
      System.err.println("usage: motif_dump <corpus.pgn>");
      System.exit(2);
    }
    System.out.print(MotifDump.dump(MotifDump.split(Files.readString(Path.of(args[0])))));
  }
}
