#!/bin/bash
set -e

echo "=== MCP OAuth 2.1 Demo Setup ==="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
KEYCLOAK_PORT=8180
MCP_SERVER_PORT=8080
CALLBACK_PORT=8888
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KEYCLOAK_DIR="$ROOT_DIR/local_docker/keycloak"
STARTED_KEYCLOAK=false
KEYCLOAK_BASE_URL="http://localhost:$KEYCLOAK_PORT"
MCP_PID=""
KEEP_KEYCLOAK="${1:-}"

# Cleanup function for signal handling
cleanup() {
    echo ""
    echo "=== Cleanup ==="
    echo -e "${YELLOW}→${NC} Stopping MCP Server..."
    # Kill bazel wrapper if we have the PID
    if [ -n "$MCP_PID" ]; then
        kill $MCP_PID 2>/dev/null || true
    fi
    # Also kill any process on the MCP port (bazel run spawns child processes)
    local port_pid
    port_pid=$(lsof -i :$MCP_SERVER_PORT -t 2>/dev/null || true)
    if [ -n "$port_pid" ]; then
        kill $port_pid 2>/dev/null || true
    fi

    if [ "$KEEP_KEYCLOAK" != "--keep-keycloak" ]; then
        if [ "$STARTED_KEYCLOAK" = true ]; then
            echo -e "${YELLOW}→${NC} Stopping Keycloak..."
            cd "$KEYCLOAK_DIR"
            docker compose -f docker-compose.keycloak.yml down
            cd "$ROOT_DIR"
        else
            echo -e "${YELLOW}→${NC} Keycloak was already running; leaving it up."
        fi
    fi
}

# Trap signals to ensure cleanup runs
trap cleanup EXIT INT TERM

echo "Configuration:"
echo "  Keycloak: http://localhost:$KEYCLOAK_PORT"
echo "  MCP Server: http://localhost:$MCP_SERVER_PORT"
echo "  OAuth Callback: http://localhost:$CALLBACK_PORT/callback"
echo ""

# Check if Keycloak is already running
if curl -sf http://localhost:$KEYCLOAK_PORT/health/ready > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} Keycloak is already running"
else
    echo -e "${YELLOW}→${NC} Starting Keycloak..."
    cd "$KEYCLOAK_DIR"
    docker compose -f docker-compose.keycloak.yml up -d
    STARTED_KEYCLOAK=true

    echo -e "${YELLOW}→${NC} Waiting for Keycloak to be ready (this may take 30-60 seconds)..."
    RETRY_COUNT=0
    MAX_RETRIES=60
    until curl -sf http://localhost:$KEYCLOAK_PORT/realms/mcp-demo/.well-known/openid-configuration > /dev/null 2>&1; do
        sleep 2
        RETRY_COUNT=$((RETRY_COUNT + 1))
        if [ $RETRY_COUNT -ge $MAX_RETRIES ]; then
            echo -e "${RED}✗${NC} Keycloak failed to start within timeout"
            echo "Checking logs..."
            docker compose -f docker-compose.keycloak.yml logs --tail 20
            exit 1
        fi
        echo -n "."
    done
    echo ""
    echo -e "${GREEN}✓${NC} Keycloak is ready"
    cd "$ROOT_DIR"
fi

# Verify Keycloak realm
echo -e "${YELLOW}→${NC} Verifying Keycloak realm configuration..."
if curl -sf http://localhost:$KEYCLOAK_PORT/realms/mcp-demo/.well-known/openid-configuration > /dev/null; then
    echo -e "${GREEN}✓${NC} Keycloak realm 'mcp-demo' is configured"
else
    echo -e "${RED}✗${NC} Keycloak realm 'mcp-demo' not found"
    echo "Please check local_docker/keycloak/realm-export.json"
    exit 1
fi

# Ensure demo client exists only when explicitly requested
if [ -z "${MCP_CLIENT_ID:-}" ] && [ "${MCP_FORCE_ADMIN_CLIENT:-}" = "1" ]; then
    echo -e "${YELLOW}→${NC} Ensuring demo client exists in Keycloak..."
    TOKEN_RESPONSE=$(curl -sS \
        -d "client_id=admin-cli" \
        -d "grant_type=password" \
        -d "username=admin" \
        -d "password=admin" \
        "$KEYCLOAK_BASE_URL/realms/master/protocol/openid-connect/token")

    if [ -z "$TOKEN_RESPONSE" ]; then
        echo -e "${RED}✗${NC} Failed to obtain Keycloak admin token (empty response)"
        exit 1
    fi

    ADMIN_TOKEN=$(python3 -c 'import json,sys; 
data = sys.stdin.read().strip(); 
print(json.loads(data).get("access_token","") if data else "")' <<< "$TOKEN_RESPONSE")

    if [ -z "$ADMIN_TOKEN" ]; then
        echo -e "${RED}✗${NC} Failed to obtain Keycloak admin token"
        echo "$TOKEN_RESPONSE"
        exit 1
    fi

    CLIENT_ID="mcp-demo-client"
    CLIENT_INTERNAL_ID=$(curl -sf \
        -H "Authorization: Bearer $ADMIN_TOKEN" \
        "$KEYCLOAK_BASE_URL/admin/realms/mcp-demo/clients?clientId=$CLIENT_ID" | \
        python3 -c 'import json,sys; 
data = json.load(sys.stdin); 
print(data[0].get("id","") if isinstance(data,list) and data else "")'
    )

    if [ -z "$CLIENT_INTERNAL_ID" ]; then
        REDIRECT_URI="http://localhost:$CALLBACK_PORT/callback"
        CREATE_STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
            -H "Authorization: Bearer $ADMIN_TOKEN" \
            -H "Content-Type: application/json" \
            -d "{
  \"clientId\": \"$CLIENT_ID\",
  \"name\": \"MCP Demo Client\",
  \"publicClient\": true,
  \"standardFlowEnabled\": true,
  \"directAccessGrantsEnabled\": false,
  \"serviceAccountsEnabled\": false,
  \"redirectUris\": [\"$REDIRECT_URI\"],
  \"webOrigins\": [\"+\"]
}" \
            "$KEYCLOAK_BASE_URL/admin/realms/mcp-demo/clients")

        if [ "$CREATE_STATUS" != "201" ]; then
            echo -e "${RED}✗${NC} Failed to create demo client (HTTP $CREATE_STATUS)"
            exit 1
        fi
        echo -e "${GREEN}✓${NC} Created demo client '$CLIENT_ID'"
    else
        echo -e "${GREEN}✓${NC} Demo client '$CLIENT_ID' already exists"
    fi

    export MCP_CLIENT_ID="$CLIENT_ID"
elif [ -n "${MCP_CLIENT_ID:-}" ]; then
    echo -e "${YELLOW}→${NC} Using preconfigured MCP client ID: ${MCP_CLIENT_ID}"
fi

# Start MCP Server with OAuth
echo -e "${YELLOW}→${NC} Starting MCP Server with OAuth enabled..."
export MICRONAUT_ENVIRONMENTS=dev
export PORT=$MCP_SERVER_PORT

bazel run //jvm/src/main/java/com/muchq/mcpserver:mcpserver > /tmp/mcp-server.log 2>&1 &
MCP_PID=$!

echo -e "${YELLOW}→${NC} Waiting for MCP Server to start..."
sleep 5

# Check if MCP server is running
if ! ps -p $MCP_PID > /dev/null; then
    echo -e "${RED}✗${NC} MCP Server failed to start"
    cat /tmp/mcp-server.log
    exit 1
fi

# Verify Protected Resource Metadata endpoint
if curl -sf http://localhost:$MCP_SERVER_PORT/.well-known/oauth-protected-resource > /dev/null; then
    echo -e "${GREEN}✓${NC} MCP Server is ready and serving OAuth metadata"
else
    echo -e "${RED}✗${NC} MCP Server not responding to OAuth metadata endpoint"
    kill $MCP_PID 2>/dev/null || true
    exit 1
fi

echo ""
echo "=== Running MCP Client Demo ==="
echo ""
echo "The demo will:"
echo "  1. Discover the authorization server from MCP server"
echo "  2. Fetch authorization server metadata from Keycloak"
echo "  3. Dynamically register as an OAuth client"
echo "  4. Open your browser for authentication"
echo "  5. Wait for you to log in (use testuser/testpass)"
echo "  6. Exchange authorization code for access token"
echo "  7. Display success message"
echo ""
echo -e "${YELLOW}Press Enter to start the demo${NC}"
read

# Run the demo
bazel run //jvm/src/main/java/com/muchq/mcpclient/demo:demo

DEMO_EXIT_CODE=$?

# Cleanup is handled by the EXIT trap

if [ $DEMO_EXIT_CODE -eq 0 ]; then
    echo ""
    echo -e "${GREEN}=== Demo Completed Successfully! ===${NC}"
    echo ""
    echo "What just happened:"
    echo "  ✓ Client discovered OAuth endpoints (RFC 9728, RFC 8414)"
    echo "  ✓ Client dynamically registered (RFC 7591)"
    echo "  ✓ PKCE protected the authorization code exchange"
    echo "  ✓ Resource parameter ensured token audience validation (RFC 8707)"
    echo "  ✓ MCP server validated the JWT token"
    echo ""
    echo "This demonstrates a complete implementation of the MCP Authorization Spec!"
else
    echo ""
    echo -e "${RED}=== Demo Failed ===${NC}"
    echo "Check the logs above for error details"
    exit 1
fi
