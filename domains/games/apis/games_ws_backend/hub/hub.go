package hub

import (
	"bytes"
	"log/slog"
	"net/http"
	"net/url"
	"os"
	"time"

	"github.com/google/uuid"
	"github.com/gorilla/websocket"
)

const (
	// Time allowed to write a message to the peer.
	writeWait = 10 * time.Second

	// Time allowed to read the next pong message from the peer.
	pongWait = 60 * time.Second

	// Send pings to peer with this period. Must be less than pongWait.
	pingPeriod = (pongWait * 9) / 10

	// Maximum message size allowed from peer.
	maxMessageSize = 512
)

var (
	newline = []byte{'\n'}
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		// Allow all origins in dev mode
		if _, isSet := os.LookupEnv("DEV_MODE"); isSet {
			slog.Debug("WebSocket connection allowed in dev mode",
				"origin", r.Header.Get("Origin"),
				"remoteAddr", r.RemoteAddr)
			return true
		}

		origin := r.Header.Get("Origin")
		if origin == "" {
			slog.Warn("WebSocket connection rejected: missing origin header",
				"remoteAddr", r.RemoteAddr)
			return false
		}

		u, err := url.Parse(origin)
		if err != nil {
			slog.Error("WebSocket connection rejected: invalid origin URL",
				"origin", origin,
				"error", err,
				"remoteAddr", r.RemoteAddr)
			return false
		}

		// Check scheme - only allow HTTPS in production
		if u.Scheme != "https" {
			slog.Warn("WebSocket connection rejected: non-HTTPS origin",
				"origin", origin,
				"scheme", u.Scheme,
				"remoteAddr", r.RemoteAddr)
			return false
		}

		// Extract hostname without port
		hostname := u.Hostname()

		// Define allowed origins exactly (no prefix matching!)
		allowedOrigins := []string{
			"thoughts.muchq.com",
			"muchq.com",
			"www.muchq.com",
		}

		// Exact match check
		for _, allowed := range allowedOrigins {
			if hostname == allowed {
				slog.Info("WebSocket connection accepted",
					"origin", origin,
					"hostname", hostname,
					"remoteAddr", r.RemoteAddr)
				return true
			}
		}

		slog.Warn("WebSocket connection rejected: unauthorized origin",
			"origin", origin,
			"hostname", hostname,
			"remoteAddr", r.RemoteAddr)
		return false
	},
}

// GameMessageData represents a message with its sender
type GameMessageData struct {
	Message []byte
	Sender  *Client
}

type Hub interface {
	Register(c *Client)
	Unregister(c *Client)
	GameMessage(data GameMessageData)
	Run()
}

// Client is an intermediary between the websocket connection and the hub.
type Client struct {
	Hub Hub

	// Unique session ID for the client
	ID string

	// The websocket connection.
	Conn *websocket.Conn

	// Buffered channel of outbound messages.
	Send chan []byte
}

// readPump pumps messages from the websocket connection to the hub.
//
// The application runs readPump in a per-connection goroutine. The application
// ensures that there is at most one reader on a connection by executing all
// reads from this goroutine.
func (c *Client) readPump() {
	defer func() {
		c.Hub.Unregister(c)
		c.Conn.Close()
	}()
	c.Conn.SetReadLimit(maxMessageSize)
	c.Conn.SetReadDeadline(time.Now().Add(pongWait))
	c.Conn.SetPongHandler(func(string) error { c.Conn.SetReadDeadline(time.Now().Add(pongWait)); return nil })
	for {
		_, message, err := c.Conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				slog.Error("WebSocket read error",
					"error", err,
					"clientAddr", c.Conn.RemoteAddr().String())
			}
			break
		}
		message = bytes.TrimSpace(message)
		c.Hub.GameMessage(GameMessageData{
			Message: message,
			Sender:  c,
		})
	}
}

// writePump pumps messages from the hub to the websocket connection.
//
// A goroutine running writePump is started for each connection. The
// application ensures that there is at most one writer to a connection by
// executing all writes from this goroutine.
func (c *Client) writePump() {
	ticker := time.NewTicker(pingPeriod)
	defer func() {
		ticker.Stop()
		c.Conn.Close()
	}()
	for {
		select {
		case message, ok := <-c.Send:
			c.Conn.SetWriteDeadline(time.Now().Add(writeWait))
			if !ok {
				// The hub closed the channel.
				c.Conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}

			w, err := c.Conn.NextWriter(websocket.TextMessage)
			if err != nil {
				return
			}
			w.Write(message)

			// Add queued chat messages to the current websocket message.
			n := len(c.Send)
			for i := 0; i < n; i++ {
				w.Write(newline)
				w.Write(<-c.Send)
			}

			if err := w.Close(); err != nil {
				return
			}
		case <-ticker.C:
			c.Conn.SetWriteDeadline(time.Now().Add(writeWait))
			if err := c.Conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

// ServeWs handles websocket requests from the peer.
func ServeWs(hub Hub, w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		slog.Error("WebSocket upgrade failed",
			"error", err,
			"remoteAddr", r.RemoteAddr,
			"origin", r.Header.Get("Origin"))
		return
	}

	slog.Debug("WebSocket client connected",
		"remoteAddr", conn.RemoteAddr().String(),
		"origin", r.Header.Get("Origin"))

	client := &Client{
		Hub:  hub,
		ID:   uuid.New().String(),
		Conn: conn,
		Send: make(chan []byte, 256),
	}
	client.Hub.Register(client)

	// Allow collection of memory referenced by the caller by doing all work in
	// new goroutines.
	go client.writePump()
	go client.readPump()
}
