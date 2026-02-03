package com.muchq.chess_com_api;

import com.muchq.http_client.jdk.Jdk11HttpClient;
import com.muchq.json.JsonUtils;
import java.net.http.HttpClient;
import java.time.YearMonth;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Demo {
  private static final Logger LOG = LoggerFactory.getLogger(Demo.class);

  public static void main(String[] args) {
    var mapper = JsonUtils.mapper();
    try (var delegate = HttpClient.newHttpClient();
        var httpClient = new Jdk11HttpClient(delegate)) {
      var chessClient = new ChessClient(httpClient, mapper);

      // read player info
      var player = chessClient.fetchPlayer("hikaru");
      LOG.info("player: {}", player);

      // read stats
      var stats = chessClient.fetchStats("hikaru");
      LOG.info("stats: {}", stats);

      // read games
      var games = chessClient.fetchGames("hikaru", YearMonth.now());
      LOG.info("games: {}", games);
    }
  }
}
