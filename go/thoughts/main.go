package main

import (
	_ "embed"
	"flag"
	"log"
	"log/slog"
	"net/http"
	"os"
)

var addr = flag.String("addr", ":8080", "http service address")

//go:embed index.html
var indexHTML []byte

func serveHome(w http.ResponseWriter, r *http.Request) {
	slog.Debug("HTTP request",
		"method", r.Method,
		"path", r.URL.Path,
		"remoteAddr", r.RemoteAddr)

	if r.URL.Path != "/" {
		http.Error(w, http.StatusText(http.StatusNotFound), http.StatusNotFound)
		return
	}
	if r.Method != http.MethodGet {
		http.Error(w, http.StatusText(http.StatusMethodNotAllowed), http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write(indexHTML)
}

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

	slog.Info("Starting thoughts game server",
		"addr", *addr,
		"devMode", os.Getenv("DEV_MODE") != "")

	hub := newHub()
	go hub.run()
	// Deprecated for removal.
	http.HandleFunc("/", serveHome)
	// Deprecated. Use /thoughts-ws
	http.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request) {
		serveWs(hub, w, r)
	})
	http.HandleFunc("/thoughts-ws", func(w http.ResponseWriter, r *http.Request) {
		serveWs(hub, w, r)
	})

	slog.Info("Server listening", "addr", *addr)
	err := http.ListenAndServe(*addr, nil)
	if err != nil {
		slog.Error("Server failed", "error", err)
		log.Fatal("ListenAndServe: ", err)
	}
}
