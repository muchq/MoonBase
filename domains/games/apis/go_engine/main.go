package main

import (
	"log"
	"net/http"
	"os"
	"os/exec"
	"strconv"

	"github.com/muchq/moonbase/domains/games/apis/go_engine"
)

func getEnv(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

func main() {
	port := getEnv("PORT", "8095")
	path := getEnv("GNUGO_PATH", "/gnugo/gnugo")
	size, err := strconv.Atoi(getEnv("ENGINES", "2"))
	if err != nil || size < 1 {
		log.Fatalf("ENGINES: want a positive count, got %q", os.Getenv("ENGINES"))
	}
	pool, err := go_engine.NewPool(size, func() (*go_engine.GTP, error) {
		return go_engine.Spawn(exec.Command(path, go_engine.GnugoArgs()...))
	})
	if err != nil {
		log.Fatalf("starting %d engines from %s: %v", size, path, err)
	}
	log.Printf("go_engine on :%s, %d engines from %s", port, size, path)
	log.Fatal(http.ListenAndServe(":"+port, go_engine.NewRouter(pool)))
}
