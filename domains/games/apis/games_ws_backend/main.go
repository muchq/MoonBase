package main

import (
	"flag"
	"log"
	"log/slog"
	"net/http"
	"os"

	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/golf"
	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/hub"
	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/players"
	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/thoughts"
)

var addr = flag.String("addr", ":8080", "http service address")

func main() {
	flag.Parse()

	// Configure structured logging
	var logLevel slog.Level
	if _, isSet := os.LookupEnv("DEV_MODE"); isSet {
		logLevel = slog.LevelDebug
	} else {
		logLevel = slog.LevelInfo
	}

	opts := &slog.HandlerOptions{
		Level: logLevel,
	}
	handler := slog.NewJSONHandler(os.Stdout, opts)
	slog.SetDefault(slog.New(handler))

	slog.Info("Starting game server",
		"addr", *addr,
		"devMode", os.Getenv("DEV_MODE") != "")

	// Serve thoughts backend
	thoughtsHub := thoughts.NewThoughtsHub()
	go thoughtsHub.Run()
	http.HandleFunc("/games/v1/thoughts-ws", func(w http.ResponseWriter, r *http.Request) {
		hub.ServeWs(thoughtsHub, w, r)
	})

	// Serve golf backend
	golfHub := golf.NewGolfHub(&players.WhimsicalIDGenerator{})
	go golfHub.Run()
	http.HandleFunc("/games/v1/golf-ws", func(w http.ResponseWriter, r *http.Request) {
		hub.ServeWs(golfHub, w, r)
	})

	slog.Info("Server listening", "addr", *addr)
	err := http.ListenAndServe(*addr, nil)
	if err != nil {
		slog.Error("Server failed", "error", err)
		log.Fatal("ListenAndServe: ", err)
	}
}
