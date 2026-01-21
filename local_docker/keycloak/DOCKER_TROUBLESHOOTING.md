# Docker Credentials Troubleshooting

## Issue

When trying to pull the Keycloak image, you may encounter:

```
error getting credentials - err: exec: "docker-credential-desktop": executable file not found in $PATH
```

## Cause

This occurs when Docker Desktop's credential helper is configured but not available in PATH, or Docker Desktop isn't properly installed/running.

## Solutions

### Option 1: Fix Docker Credential Helper (Recommended)

1. **Check if Docker Desktop is running:**
   ```bash
   open -a Docker  # macOS
   ```

2. **Verify Docker credential helper path:**
   ```bash
   which docker-credential-desktop
   # Should output: /Applications/Docker.app/Contents/Resources/bin/docker-credential-desktop
   ```

3. **Add to PATH if missing:**
   ```bash
   export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
   # Add to ~/.zshrc or ~/.bashrc for persistence
   ```

### Option 2: Disable Credential Helper Temporarily

1. **Edit Docker config:**
   ```bash
   vim ~/.docker/config.json
   ```

2. **Remove or comment out `credsStore`:**
   ```json
   {
     "auths": {},
     // "credsStore": "desktop"  <- Comment or remove this line
   }
   ```

3. **Try pulling again:**
   ```bash
   docker pull quay.io/keycloak/keycloak:26.0
   ```

### Option 3: Use Alternative Keycloak Image

If `quay.io` is the issue, try Docker Hub:

1. **Update `docker-compose.keycloak.yml`:**
   ```yaml
   services:
     keycloak:
       image: keycloak/keycloak:26.0  # Docker Hub instead of quay.io
   ```

### Option 4: Build from Dockerfile

If pulling images continues to fail, create a minimal Dockerfile:

```dockerfile
FROM eclipse-temurin:21-jre-jammy
WORKDIR /opt/keycloak
# Download Keycloak release manually
```

## Verification

After applying a fix, verify Docker works:

```bash
# Test pulling a small image
docker pull hello-world

# Test Keycloak pull
docker pull quay.io/keycloak/keycloak:26.0

# Start Keycloak
cd local_docker/keycloak
docker compose -f docker-compose.keycloak.yml up -d
```

## References

- [Docker Credential Helpers](https://docs.docker.com/engine/reference/commandline/login/#credential-helpers)
- [Docker Desktop Installation](https://docs.docker.com/desktop/install/mac-install/)
