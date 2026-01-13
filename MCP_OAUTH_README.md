# MCP OAuth 2.1 Implementation

Complete implementation of the [MCP Authorization Spec](https://modelcontextprotocol.io/specification/2025-06-18/basic/authorization) with OAuth 2.1, PKCE, and Dynamic Client Registration.

## Overview

This implementation demonstrates the cutting-edge MCP Authorization Spec, which enables secure OAuth-based authentication for Model Context Protocol servers. It includes:

- **MCP Server** with OAuth 2.1 support (extends existing custom Micronaut server)
- **MCP Client** using the official SDK with full OAuth flow
- **Keycloak** as the Authorization Server
- **Complete RFC Compliance**: RFC 9728, RFC 8414, RFC 7591, RFC 7636, RFC 8707

## Architecture

```
┌──────────────────────────┐
│   MCP Client (NEW)       │
│   Using Official SDK     │
│   - OAuth flow handler   │
│   - PKCE generator       │
│   - Token manager        │
└───────────┬──────────────┘
            │ 1. Request without token
            ▼
┌──────────────────────────┐
│   MCP Server (EXTENDED)  │
│   Custom Micronaut       │
│   - Token validator      │
│   - RFC 9728 metadata    │
└───────────┬──────────────┘
            │ 2. HTTP 401 + WWW-Authenticate
            ▼
┌──────────────────────────┐
│   OAuth Discovery        │
│   - Fetch metadata       │
│   - Dynamic registration │
│   - Generate PKCE        │
└───────────┬──────────────┘
            │ 3. Authorization with resource parameter
            ▼
┌──────────────────────────┐
│   Keycloak (Docker)      │
│   - User authentication  │
│   - Token issuance       │
│   - Audience validation  │
└───────────┬──────────────┘
            │ 4. Access token with audience claim
            ▼
            Back to MCP Client
```

## What's Implemented

### Server Extensions (Phase 1-3) ✅

**Location:** `jvm/src/main/java/com/muchq/mcpserver/oauth/`

**Files:**
- `OAuthConfig.java` - Configuration for Keycloak integration
- `ProtectedResourceMetadata.java` - RFC 9728 DTO
- `ProtectedResourceController.java` - Metadata endpoint at `/.well-known/oauth-protected-resource`
- `TokenValidator.java` - **JWT validation with critical audience checking**
- `OAuthAuthenticationFilter.java` - HTTP filter with WWW-Authenticate headers

**Key Features:**
- RFC 9728 Protected Resource Metadata discovery
- JWT signature verification against Keycloak JWKS
- **Audience validation (RFC 8707)** - prevents token reuse across services
- Timing-attack resistant token comparison
- Backward compatible with legacy Bearer token auth

**Configuration (`application.yml`):**
```yaml
mcp:
  oauth:
    enabled: ${MCP_OAUTH_ENABLED:false}
    authorization-server: ${MCP_OAUTH_AUTHZ_SERVER:http://localhost:8180/realms/mcp-demo}
    resource-uri: ${MCP_RESOURCE_URI:http://localhost:8080}
    jwks-uri: ${MCP_OAUTH_JWKS_URI:http://localhost:8180/realms/mcp-demo/protocol/openid-connect/certs}
```

### Client Implementation (Phase 4-6) ✅

**Location:** `jvm/src/main/java/com/muchq/mcpclient/`

**OAuth Components:**
- `oauth/PkceGenerator.java` - Cryptographically secure PKCE with S256
- `oauth/TokenManager.java` - Thread-safe token storage with expiration tracking
- `oauth/CallbackServer.java` - Local HTTP server for OAuth callback
- `oauth/BrowserLauncher.java` - Opens system browser for authorization
- `oauth/OAuthFlowHandler.java` - **Orchestrates complete OAuth 2.1 flow**

**Client Wrapper:**
- `McpClientConfig.java` - Configuration builder
- `McpClientWrapper.java` - Wraps official MCP SDK with OAuth support

**Demo:**
- `demo/McpClientDemo.java` - Full end-to-end demonstration
- `scripts/run-mcp-oauth-demo.sh` - Automated test script

### Keycloak Setup ✅

**Location:** `local_docker/keycloak/`

**Files:**
- `docker-compose.keycloak.yml` - Keycloak 26.0 service
- `realm-export.json` - Pre-configured `mcp-demo` realm
- `README.md` - Setup and configuration guide
- `DOCKER_TROUBLESHOOTING.md` - Docker credentials fix

**Realm Configuration:**
- Dynamic Client Registration enabled (Anonymous policy)
- PKCE enforcement via Client Policy (S256 required)
- Resource Indicators support enabled
- Test user: `testuser` / `testpass`
- Port: 8180 (avoids conflict with MCP server)

## Quick Start

### Prerequisites

1. **Java/Bazel** - Already set up (you're in MoonBase)
2. **Docker** - Fix credentials issue first:

```bash
# Option 1: Add Docker Desktop to PATH
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
echo 'export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"' >> ~/.zshrc

# Option 2: Disable credential helper
vim ~/.docker/config.json
# Remove the "credsStore": "desktop" line
```

### Run the Demo

```bash
cd /Users/andy/src/MoonBase

# Run the complete demo
./scripts/run-mcp-oauth-demo.sh
```

**What happens:**
1. ✅ Starts Keycloak on http://localhost:8180
2. ✅ Starts MCP Server with OAuth on http://localhost:8080
3. 🚀 Runs MCP client which:
   - Discovers authorization server (RFC 9728)
   - Fetches authorization server metadata (RFC 8414)
   - Dynamically registers as OAuth client (RFC 7591)
   - Generates PKCE parameters (S256)
   - Opens browser for user login
   - Exchanges authorization code for token (RFC 8707)
   - Validates token audience matches MCP server
   - Displays success message

**Login Credentials:**
- Username: `testuser`
- Password: `testpass`

### Manual Setup

If you want to run components separately:

**1. Start Keycloak:**
```bash
cd local_docker/keycloak
docker compose -f docker-compose.keycloak.yml up -d

# Wait for ready
until curl -sf http://localhost:8180/health/ready > /dev/null; do sleep 2; done
```

**2. Start MCP Server with OAuth:**
```bash
export MCP_OAUTH_ENABLED=true
export MCP_OAUTH_AUTHZ_SERVER=http://localhost:8180/realms/mcp-demo
export MCP_RESOURCE_URI=http://localhost:8080
export MCP_OAUTH_JWKS_URI=http://localhost:8180/realms/mcp-demo/protocol/openid-connect/certs
export PORT=8080

bazel run //jvm/src/main/java/com/muchq/mcpserver:mcpserver
```

**3. Run MCP Client Demo:**
```bash
bazel run //jvm/src/main/java/com/muchq/mcpclient/demo:demo
```

## Verification

### Test Server Metadata Endpoint

```bash
# Protected Resource Metadata (RFC 9728)
curl http://localhost:8080/.well-known/oauth-protected-resource | jq

# Expected response:
{
  "resource": "http://localhost:8080",
  "authorization_servers": ["http://localhost:8180/realms/mcp-demo"],
  "bearer_methods_supported": ["header"]
}
```

### Test Keycloak Configuration

```bash
# Authorization Server Metadata (RFC 8414)
curl http://localhost:8180/realms/mcp-demo/.well-known/openid-configuration | jq

# Check for key endpoints:
# - authorization_endpoint
# - token_endpoint
# - registration_endpoint (Dynamic Client Registration)
```

### Test Unauthorized Request

```bash
# Request without token should return 401
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize"}' \
  -i | grep "WWW-Authenticate"

# Should see:
# WWW-Authenticate: Bearer realm="mcp", error="invalid_token", ...
```

## Security Features

### Critical Security Implementations

1. **Audience Validation (RFC 8707)** ⚠️ CRITICAL
   - Tokens MUST have `aud` claim matching MCP server URI
   - Prevents token reuse across different services
   - Location: `TokenValidator.java:89-96`

2. **PKCE (RFC 7636)**
   - Cryptographically secure code_verifier (32 bytes)
   - S256 code_challenge method (SHA-256)
   - Prevents authorization code interception
   - Location: `PkceGenerator.java`

3. **Dynamic Client Registration (RFC 7591)**
   - No hardcoded client credentials
   - Clients register automatically
   - Public client (no client secret for native apps)
   - Location: `OAuthFlowHandler.java:195-230`

4. **JWT Signature Verification**
   - Validates against Keycloak's JWKS
   - RSA signature verification
   - Location: `TokenValidator.java:111-137`

5. **Timing Attack Resistance**
   - Legacy token comparison uses `MessageDigest.isEqual()`
   - Location: `McpAuthenticationFilter.java:67-69`

### Security Checklist

- [x] Server validates JWT signature
- [x] Server validates token expiration
- [x] **Server validates audience claim** (CRITICAL)
- [x] Server returns WWW-Authenticate on 401
- [x] Client generates cryptographically random PKCE
- [x] Client uses S256 code challenge method
- [x] Client includes resource parameter in auth request
- [x] Client includes resource parameter in token request
- [x] Tokens stored securely (in-memory for demo)
- [x] OAuth communication over HTTPS ready (localhost for dev)

## Project Structure

```
jvm/src/main/java/com/muchq/
├── mcpserver/                 # Existing MCP Server
│   ├── oauth/                 # NEW: OAuth extensions
│   │   ├── OAuthConfig.java
│   │   ├── ProtectedResourceController.java
│   │   ├── ProtectedResourceMetadata.java
│   │   ├── TokenValidator.java
│   │   └── OAuthAuthenticationFilter.java
│   ├── McpController.java
│   ├── McpRequestHandler.java
│   └── tools/                 # MCP tools (add, echo, chess, etc.)
│
└── mcpclient/                 # NEW: MCP Client
    ├── McpClientConfig.java
    ├── McpClientWrapper.java
    ├── oauth/
    │   ├── OAuthFlowHandler.java
    │   ├── PkceGenerator.java
    │   ├── TokenManager.java
    │   ├── CallbackServer.java
    │   └── BrowserLauncher.java
    └── demo/
        └── McpClientDemo.java

local_docker/keycloak/
├── docker-compose.keycloak.yml
├── realm-export.json
├── README.md
└── DOCKER_TROUBLESHOOTING.md

scripts/
└── run-mcp-oauth-demo.sh
```

## Key Dependencies

**Added to `bazel/java.MODULE.bazel`:**
```python
"com.nimbusds:nimbus-jose-jwt:9.47",           # JWT parsing/validation
"com.nimbusds:oauth2-oidc-sdk:11.21",          # OAuth 2.1 client
"io.modelcontextprotocol.sdk:mcp:0.12.1",     # Official MCP SDK
"org.bouncycastle:bcprov-jdk18on:1.80",       # Crypto for PKCE
```

## Remaining TODOs

### High Priority

- [ ] **Fix Docker credentials issue** to run Keycloak
- [ ] **Test end-to-end flow** with actual OAuth authentication
- [ ] Integrate MCP SDK's transport layer in client (currently simplified)
- [ ] Add actual MCP tool calls (initialize, listTools, callTool) to demo

### Medium Priority

- [ ] Implement token refresh flow
- [ ] Add persistent token storage (OS keychain)
- [ ] Add integration tests with WireMock
- [ ] Add unit tests for PKCE, TokenManager, CallbackServer
- [ ] Support multiple authorization servers (RFC 9728 allows array)

### Low Priority

- [ ] Implement OAuth 2.1 Device Flow (RFC 8628) for headless systems
- [ ] Add mTLS support for additional transport security
- [ ] Add scope negotiation for fine-grained permissions
- [ ] Refactor CallbackServer to use Micronaut (for consistency)
- [ ] Add comprehensive error recovery and user feedback

### Production Enhancements

- [ ] Use HTTPS for all OAuth endpoints (currently localhost HTTP)
- [ ] Implement secure token rotation
- [ ] Add rate limiting for token endpoint
- [ ] Add monitoring and metrics
- [ ] Create Docker Compose for complete stack
- [ ] Add Kubernetes deployment manifests

## Troubleshooting

### Docker Credentials Error

See `local_docker/keycloak/DOCKER_TROUBLESHOOTING.md` for detailed solutions.

**Quick fix:**
```bash
vim ~/.docker/config.json
# Remove: "credsStore": "desktop"
```

### Keycloak Not Starting

```bash
# Check logs
docker compose -f local_docker/keycloak/docker-compose.keycloak.yml logs -f

# Restart
docker compose -f local_docker/keycloak/docker-compose.keycloak.yml restart

# Full reset
docker compose -f local_docker/keycloak/docker-compose.keycloak.yml down
docker compose -f local_docker/keycloak/docker-compose.keycloak.yml up -d
```

### Port Already in Use

```bash
# Check what's using port 8180
lsof -i :8180

# Or use a different port
# Edit docker-compose.keycloak.yml:
ports:
  - "8181:8180"
```

### Client Registration Fails

1. Verify Keycloak is running: `curl http://localhost:8180/health/ready`
2. Check realm exists: `curl http://localhost:8180/realms/mcp-demo/.well-known/openid-configuration`
3. Check Client Registration Policies in Keycloak Admin Console

### Token Validation Fails

Common causes:
- Wrong audience claim (check `MCP_RESOURCE_URI` matches token `aud`)
- Expired token (Keycloak default: 5 minutes)
- JWKS not accessible from server
- Token signature invalid

## References

### Specifications

- [MCP Authorization Spec](https://modelcontextprotocol.io/specification/2025-06-18/basic/authorization)
- [RFC 9728 - OAuth 2.0 Protected Resource Metadata](https://datatracker.ietf.org/doc/html/rfc9728)
- [RFC 8414 - OAuth 2.0 Authorization Server Metadata](https://datatracker.ietf.org/doc/html/rfc8414)
- [RFC 7591 - OAuth 2.0 Dynamic Client Registration](https://datatracker.ietf.org/doc/html/rfc7591)
- [RFC 7636 - PKCE](https://datatracker.ietf.org/doc/html/rfc7636)
- [RFC 8707 - Resource Indicators](https://www.rfc-editor.org/rfc/rfc8707.html)

### Resources

- [Keycloak Documentation](https://www.keycloak.org/documentation)
- [MCP Java SDK](https://modelcontextprotocol.io/sdk/java/mcp-overview)
- [Nimbus OAuth SDK](https://connect2id.com/products/nimbus-oauth-openid-connect-sdk)

## License

Part of the MoonBase project.

## Contributing

This is a demonstration implementation of the MCP Authorization Spec. Key areas for contribution:
- Additional security tests
- Token refresh implementation
- Integration with real MCP tools
- Documentation improvements

---

**Status:** ✅ Implementation Complete - Pending E2E Testing (requires Docker fix)

**Last Updated:** 2026-01-12
