package main

import (
	"log"
	"net/http"
	"os"
	"os/exec"
	"strconv"

	"github.com/muchq/moonbase/domains/games/apis/chess_engine"
)

func getEnv(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

func main() {
	port := getEnv("PORT", "8094")
	path := getEnv("STOCKFISH_PATH", "/stockfish/stockfish")
	size, err := strconv.Atoi(getEnv("ENGINES", "2"))
	if err != nil || size < 1 {
		log.Fatalf("ENGINES: want a positive count, got %q", os.Getenv("ENGINES"))
	}
	pool, err := chess_engine.NewPool(size, func() (*chess_engine.UCI, error) {
		return chess_engine.Spawn(exec.Command(path))
	})
	if err != nil {
		log.Fatalf("starting %d engines from %s: %v", size, path, err)
	}
	defer pool.Close()
	log.Printf("chess_engine on :%s, %d engines from %s", port, size, path)
	log.Fatal(http.ListenAndServe(":"+port, chess_engine.NewRouter(pool)))
}
