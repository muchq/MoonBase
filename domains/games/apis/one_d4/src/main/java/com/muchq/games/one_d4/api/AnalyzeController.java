package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.AnalyzeRequest;
import com.muchq.games.one_d4.api.dto.AnalyzeResponse;
import com.muchq.games.one_d4.service.PositionAnalyzer;
import jakarta.inject.Singleton;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Motif analysis of one PGN, indexing nothing.
 *
 * <p>The read endpoints answer questions about the corpus; this one answers a question about a game
 * the caller brought, and touches no storage at all. It exists so that every chess-analysis client
 * — {@code analyze_position} over MCP today, anything else later — gets its motifs from the same
 * detectors and the same derivation rules as the indexer, rather than from a second copy of that
 * logic in another process.
 *
 * <p>Not routed publicly through Caddy. Analysis is unauthenticated CPU on caller-supplied input,
 * which is fine between services on the internal network and a different decision on the open
 * internet; {@link PositionAnalyzer} bounds the input size and the wall clock either way.
 */
@Singleton
@Path("/v1/analyze")
public class AnalyzeController {
  private static final Logger LOG = LoggerFactory.getLogger(AnalyzeController.class);

  private final PositionAnalyzer analyzer;

  public AnalyzeController(PositionAnalyzer analyzer) {
    this.analyzer = analyzer;
  }

  @POST
  @Consumes(MediaType.APPLICATION_JSON)
  @Produces(MediaType.APPLICATION_JSON)
  public AnalyzeResponse analyze(AnalyzeRequest request) {
    // The PGN itself is not logged: it is caller-supplied game text, potentially large, and of no
    // use in a log line. Its size is the part worth having when a timeout shows up.
    LOG.info(
        "POST /v1/analyze pgnBytes={}",
        request == null || request.pgn() == null ? 0 : request.pgn().length());
    return analyzer.analyze(request == null ? null : request.pgn());
  }
}
