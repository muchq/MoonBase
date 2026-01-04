# WebSocket Chat Server

A basic WebSocket chat server built with Micronaut and Netty. Supports real-time messaging with automatic username generation and user tracking.

## Features

- Real-time WebSocket communication
- Automatic username assignment (User-XXXXXXXX)
- User join/leave notifications
- Active user count tracking
- Broadcast messaging to all connected clients

## Build the Java binary

```bash
bazel build //jvm/src/main/java/com/muchq/chatserver:chatserver
```

## Build the Docker image

```bash
bazel build //jvm/src/main/java/com/muchq/chatserver:oci_tarball
```

## Run with Docker

```bash
# Load the image into Docker
bazel run //jvm/src/main/java/com/muchq/chatserver:oci_tarball

# Run the container
docker run -p 8080:8080 chatserver:latest

# Run on custom port
docker run -e PORT=9090 -p 9090:9090 chatserver:latest
```

## Run locally

```bash
bazel run //jvm/src/main/java/com/muchq/chatserver:chatserver
```

## Testing

You can test the WebSocket chat server using a simple HTML client or command-line tools like `websocat`.

### Using websocat (command-line)

```bash
# Install websocat
# brew install websocat  # macOS
# cargo install websocat  # via Rust

# Connect to the chat server
websocat ws://localhost:8080/chat

# Send messages (JSON format)
{"text": "Hello, everyone!"}
```

### Using a simple HTML client

```html
<!DOCTYPE html>
<html>
<head>
    <title>WebSocket Chat</title>
</head>
<body>
    <div id="messages"></div>
    <input type="text" id="messageInput" placeholder="Type a message...">
    <button onclick="sendMessage()">Send</button>

    <script>
        const ws = new WebSocket('ws://localhost:8080/chat');

        ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            const div = document.getElementById('messages');
            div.innerHTML += `<p><b>${data.username}:</b> ${data.text} (${data.userCount} users)</p>`;
        };

        function sendMessage() {
            const input = document.getElementById('messageInput');
            ws.send(JSON.stringify({ text: input.value }));
            input.value = '';
        }

        document.getElementById('messageInput').addEventListener('keypress', (e) => {
            if (e.key === 'Enter') sendMessage();
        });
    </script>
</body>
</html>
```

## WebSocket Protocol

### Client → Server

Send messages as JSON:
```json
{
  "text": "Your message here"
}
```

### Server → Client

Receive broadcasts as JSON:
```json
{
  "username": "User-abc12345",
  "text": "Message content",
  "userCount": 3
}
```

System messages use `"system"` as the username for join/leave notifications.

## Configuration

- **PORT**: Server port (default: 8080)
- **APP_NAME**: Application name (default: chatserver)

## WebSocket Endpoint

- **Path**: `/chat`
- **URL**: `ws://localhost:8080/chat`
