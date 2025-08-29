# Portrait Ray Tracer PNG Download Support - Task List

## Overview
This document outlines the remaining tasks for the ray tracer output PNG download support in the cpp/portrait service.

## Remaining Tasks

### 1. HTTP Endpoint Enhancements
- [ ] Add optional `format` parameter (png/base64/raw)
- [ ] Add size information to response

### 2. Rate Limiting (PRIORITY)
- [ ] Implement per-IP rate limiting for /v1/trace endpoint
- [ ] Add configurable rate limit (e.g., 10 requests per minute)
- [ ] Return 429 Too Many Requests with retry-after header
- [ ] Consider using token bucket or sliding window algorithm
- [ ] Add rate limit headers (X-RateLimit-Limit, X-RateLimit-Remaining)
- [ ] Implement bypass mechanism for authenticated/premium users

### 3. Error Handling Improvements
- [ ] Handle out-of-memory conditions for large images
- [ ] Implement timeout for long-running ray traces
- [ ] Add graceful degradation for unsupported features

### 4. Performance Optimization
- [ ] Add multi-threading support for ray tracing
- [ ] Implement progressive rendering with status updates
- [ ] Profile and optimize hot paths

### 5. API Documentation
- [ ] Document request/response formats
- [ ] Add example JSON payloads
- [ ] Create OpenAPI/Swagger specification
- [ ] Document performance characteristics

### 6. Client Integration Support
- [ ] Provide JavaScript example for consuming base64 PNG
- [ ] Create sample HTML page with download functionality
- [ ] Support streaming for large images

### 7. Monitoring and Logging
- [ ] Add metrics for render time per request
- [ ] Log scene complexity statistics
- [ ] Track PNG generation performance
- [ ] Monitor memory usage during rendering

### 8. Configuration and Tuning
- [ ] Add config for maximum image dimensions
- [ ] Configure ray tracing recursion depth
- [ ] Set memory limits for rendering
- [ ] Add quality vs. performance trade-off settings

## Immediate Next Steps (Priority Order)

1. **Implement Rate Limiting** - Protect service from abuse
   - Add per-IP rate limiting with configurable limits
   - Implement proper 429 responses with retry-after
   - Add rate limit headers for client awareness
   
2. **Enhance Response Format Options** - Add flexibility to output formats
   - Add optional `format` parameter to `/v1/trace` (png/base64/raw)
   - Include file size information in response
   - Add render_time_ms to TraceResponse

3. **Error handling improvements** - Add validation and timeout handling
   - Memory limits for large images
   - Timeout for long ray traces
   - Proper error responses with details

## Performance Considerations
- Ray tracing is CPU-intensive; consider parallelization
- Large images require significant memory
- Base64 encoding increases payload size by ~33%
- Consider implementing progressive rendering for UX

## Security Considerations
- Implement request rate limiting (HIGH PRIORITY)
- Monitor and log suspicious activity