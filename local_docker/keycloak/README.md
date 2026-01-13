# Keycloak Setup for MCP OAuth Demo

This directory contains Keycloak configuration for demonstrating the MCP Authorization Spec with OAuth 2.1 + PKCE + Dynamic Client Registration.

## Quick Start

```bash
# Start Keycloak
docker compose -f docker-compose.keycloak.yml up -d

# Wait for Keycloak to be ready (about 30 seconds)
until curl -sf http://localhost:8180/health/ready > /dev/null; do sleep 2; done

# Verify it's running
curl http://localhost:8180/realms/mcp-demo/.well-known/openid-configuration | jq

# Stop Keycloak
docker compose -f docker-compose.keycloak.yml down
```

## Configuration

### Realm: `mcp-demo`

- **Port:** `8180` (avoids conflict with MCP server on `8080`)
- **Admin Console:** http://localhost:8180/admin
- **Admin Credentials:** `admin` / `admin`

### Test User

- **Username:** `testuser`
- **Password:** `testpass`
- **Email:** testuser@example.com

### OAuth 2.1 Features Enabled

1. **Dynamic Client Registration (RFC 7591)**
   - Anonymous registration enabled via Client Registration Policies
   - Clients can self-register at `/realms/mcp-demo/clients-registrations/openid-connect`

2. **PKCE Enforcement (RFC 7636)**
   - Client Policy: `mcp-oauth-policy`
   - Client Profile: `mcp-oauth-profile`
   - PKCE is automatically enforced for all dynamically registered clients
   - Only S256 code challenge method is supported

3. **Resource Indicators (RFC 8707)**
   - Supported via custom `resource` parameter in authorization/token requests
   - Tokens will include `aud` claim based on resource parameter

4. **Short-Lived Tokens**
   - Access Token Lifespan: 5 minutes (300 seconds)
   - Refresh Token: Enabled for token renewal

## Key Endpoints

### Well-Known Endpoints

```bash
# OpenID Configuration
http://localhost:8180/realms/mcp-demo/.well-known/openid-configuration

# JWKS (for token validation)
http://localhost:8180/realms/mcp-demo/protocol/openid-connect/certs
```

### OAuth Endpoints

```bash
# Authorization Endpoint
http://localhost:8180/realms/mcp-demo/protocol/openid-connect/auth

# Token Endpoint
http://localhost:8180/realms/mcp-demo/protocol/openid-connect/token

# Dynamic Client Registration
http://localhost:8180/realms/mcp-demo/clients-registrations/openid-connect
```

## Testing Dynamic Client Registration

```bash
# Register a new client
curl -X POST http://localhost:8180/realms/mcp-demo/clients-registrations/openid-connect \
  -H "Content-Type: application/json" \
  -d '{
    "client_name": "Test MCP Client",
    "redirect_uris": ["http://localhost:8888/callback"],
    "grant_types": ["authorization_code", "refresh_token"],
    "response_types": ["code"],
    "token_endpoint_auth_method": "none"
  }' | jq

# Response will include client_id and client_secret (if applicable)
```

## Verifying PKCE Enforcement

The realm is configured to automatically enforce PKCE for all dynamically registered clients:

```bash
# Authorization request (MUST include code_challenge)
http://localhost:8180/realms/mcp-demo/protocol/openid-connect/auth?\
  response_type=code&\
  client_id=<client_id>&\
  redirect_uri=http://localhost:8888/callback&\
  code_challenge=<base64url(SHA256(code_verifier))>&\
  code_challenge_method=S256

# Token exchange (MUST include code_verifier)
POST http://localhost:8180/realms/mcp-demo/protocol/openid-connect/token
Content-Type: application/x-www-form-urlencoded

grant_type=authorization_code&
code=<authorization_code>&
redirect_uri=http://localhost:8888/callback&
client_id=<client_id>&
code_verifier=<original_verifier>
```

## Client Policies Configuration

The realm includes pre-configured client policies for OAuth 2.1 compliance:

### Policy: `mcp-oauth-policy`

- **Applies To:** Clients registered via anonymous registration
- **Enforces:** `mcp-oauth-profile`

### Profile: `mcp-oauth-profile`

- **Executors:**
  - `pkce-enforcer`: Automatically requires PKCE for authorization code flow
  - `secure-client-authn-executor`: Restricts client authentication methods

## Troubleshooting

### Port Already in Use

If port 8180 is already in use, modify `docker-compose.keycloak.yml`:

```yaml
ports:
  - "8181:8180"  # Change host port to 8181
```

Then update MCP server configuration:

```bash
export MCP_OAUTH_AUTHZ_SERVER=http://localhost:8181/realms/mcp-demo
```

### Keycloak Not Starting

Check logs:

```bash
docker compose -f docker-compose.keycloak.yml logs -f
```

### Realm Not Imported

If the realm doesn't exist after startup, manually import it:

1. Open Admin Console: http://localhost:8180/admin
2. Login with `admin` / `admin`
3. Click "Create Realm"
4. Click "Browse" and select `realm-export.json`
5. Click "Create"

### Dynamic Registration Failing

Verify client registration policies are active:

1. Admin Console → Realm Settings → Client Registration
2. Check "Anonymous" access type is enabled
3. Admin Console → Realm Settings → Client Policies
4. Verify `mcp-oauth-policy` is enabled and active

## Integration with MCP Server

The MCP server should be configured to use this Keycloak instance:

```bash
export MCP_OAUTH_ENABLED=true
export MCP_OAUTH_AUTHZ_SERVER=http://localhost:8180/realms/mcp-demo
export MCP_RESOURCE_URI=http://localhost:8080
export MCP_OAUTH_JWKS_URI=http://localhost:8180/realms/mcp-demo/protocol/openid-connect/certs
```

The MCP server will:
- Advertise Keycloak as its authorization server in RFC 9728 metadata
- Validate JWT tokens against Keycloak's JWKS
- Verify token audience matches `http://localhost:8080`

## Security Notes

- This is a **development-only** configuration
- SSL is disabled (`sslRequired: none`) for localhost testing
- In production:
  - Enable HTTPS
  - Use proper SSL certificates
  - Configure secure redirect URIs (no localhost)
  - Enable brute force protection
  - Configure proper session timeouts
  - Use external database (not in-memory)

## References

- [Keycloak Documentation](https://www.keycloak.org/documentation)
- [OAuth 2.1 Draft](https://datatracker.ietf.org/doc/html/draft-ietf-oauth-v2-1-13)
- [RFC 7591 - Dynamic Client Registration](https://datatracker.ietf.org/doc/html/rfc7591)
- [RFC 7636 - PKCE](https://datatracker.ietf.org/doc/html/rfc7636)
- [RFC 8707 - Resource Indicators](https://www.rfc-editor.org/rfc/rfc8707.html)
- [MCP Authorization Spec](https://modelcontextprotocol.io/specification/2025-06-18/basic/authorization)
