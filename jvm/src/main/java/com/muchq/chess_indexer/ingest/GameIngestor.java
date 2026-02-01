package com.muchq.chess_indexer.ingest;

import com.muchq.chess_com_api.ChessClient;
import com.muchq.chess_com_api.GamesResponse;
import com.muchq.chess_com_api.PlayedGame;
import com.muchq.chess_indexer.db.GameDao;
import com.muchq.chess_indexer.db.GameFeaturesDao;
import com.muchq.chess_indexer.db.GameMotifDao;
import com.muchq.chess_indexer.features.FeatureExtractionResult;
import com.muchq.chess_indexer.features.FeatureExtractor;
import com.muchq.chess_indexer.features.Motif;
import com.muchq.chess_indexer.model.GameRecord;
import com.muchq.chess_indexer.model.IndexRequest;
import jakarta.inject.Singleton;
import java.time.Instant;
import java.time.LocalDate;
import java.time.LocalTime;
import java.time.YearMonth;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

@Singleton
public class GameIngestor {

  private final ChessClient chessClient;
  private final GameDao gameDao;
  private final GameFeaturesDao featuresDao;
  private final GameMotifDao motifDao;
  private final FeatureExtractor extractor;

  public GameIngestor(ChessClient chessClient,
                      GameDao gameDao,
                      GameFeaturesDao featuresDao,
                      GameMotifDao motifDao) {
    this.chessClient = chessClient;
    this.gameDao = gameDao;
    this.featuresDao = featuresDao;
    this.motifDao = motifDao;
    this.extractor = new FeatureExtractor();
  }

  public int ingest(IndexRequest request) {
    List<YearMonth> months = monthsBetween(request.startDate(), request.endDate());
    Instant startInstant = request.startDate().atStartOfDay(ZoneOffset.UTC).toInstant();
    Instant endInstant = request.endDate().atTime(LocalTime.MAX).atZone(ZoneOffset.UTC).toInstant();

    int count = 0;
    for (YearMonth month : months) {
      Optional<GamesResponse> response = chessClient.fetchGames(request.username(), month);
      if (response.isEmpty()) {
        continue;
      }
      for (PlayedGame game : response.get().games()) {
        if (game.endTime().isBefore(startInstant) || game.endTime().isAfter(endInstant)) {
          continue;
        }
        UUID gameId = persistGame(request, game);
        if (gameId != null) {
          count++;
        }
      }
    }
    return count;
  }

  private UUID persistGame(IndexRequest request, PlayedGame playedGame) {
    GameRecord record = new GameRecord(
        null,
        request.platform(),
        playedGame.uuid(),
        playedGame.endTime(),
        playedGame.rated(),
        playedGame.timeClass(),
        playedGame.rules(),
        playedGame.eco(),
        playedGame.whiteResult().username(),
        playedGame.whiteResult().rating(),
        playedGame.blackResult().username(),
        playedGame.blackResult().rating(),
        resultString(playedGame),
        playedGame.pgn()
    );

    UUID gameId = gameDao.upsert(record);
    gameDao.linkToRequest(request.id(), gameId);

    FeatureExtractionResult extraction = extractor.extract(gameId, playedGame.pgn());
    featuresDao.upsert(extraction.features());
    motifDao.replaceMotifs(gameId, toMotifRecords(extraction.motifs()));

    return gameId;
  }

  private List<GameMotifDao.MotifRecord> toMotifRecords(List<Motif> motifs) {
    List<GameMotifDao.MotifRecord> records = new ArrayList<>(motifs.size());
    for (Motif motif : motifs) {
      records.add(new GameMotifDao.MotifRecord(motif.name(), motif.firstPly()));
    }
    return records;
  }

  private String resultString(PlayedGame game) {
    String white = game.whiteResult().result();
    String black = game.blackResult().result();
    if ("win".equalsIgnoreCase(white)) {
      return "1-0";
    }
    if ("win".equalsIgnoreCase(black)) {
      return "0-1";
    }
    if ("agreed".equalsIgnoreCase(white) || "agreed".equalsIgnoreCase(black)) {
      return "1/2-1/2";
    }
    return "*";
  }

  private List<YearMonth> monthsBetween(LocalDate start, LocalDate end) {
    List<YearMonth> months = new ArrayList<>();
    YearMonth current = YearMonth.from(start);
    YearMonth last = YearMonth.from(end);

    while (!current.isAfter(last)) {
      months.add(current);
      current = current.plusMonths(1);
    }
    return months;
  }
}
