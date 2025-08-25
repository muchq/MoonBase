# Portrait Ray Tracer PNG Download Support - Task List

## Overview
This document outlines the tasks required to add ray tracer output PNG download support to the cpp/portrait service.

## Detailed Tasks

### 1. Core Ray Tracing Integration
- [ ] Import and adapt the ray tracing logic from `cpp/tracy` module
- [ ] Create a `TracerService` class that wraps the tracy::Tracer functionality
- [ ] Map portrait data structures (Vec3, Color, Sphere, Light, Scene) to tracy equivalents
- [ ] Handle coordinate system conversions between portrait and tracy

### 2. Ray Tracing Service Implementation
- [ ] Create `tracer_service.h` and `tracer_service.cc` files
- [ ] Implement scene conversion from portrait::TraceRequest to tracy::Scene
- [ ] Add viewport and projection plane calculations based on output dimensions
- [ ] Implement camera positioning and orientation from Perspective data

### 3. Image Rendering Pipeline
- [ ] Set up Image<RGB_Double> buffer for ray tracing output
- [ ] Implement the render method that calls tracy::Tracer::drawScene
- [ ] Convert RGB_Double values to RGB for PNG export
- [ ] Handle image dimensions from Output specification

### 4. PNG Generation
- [ ] Integrate `cpp/png_plusplus` library for PNG writing
- [ ] Create in-memory PNG buffer instead of file-based output
- [ ] Implement PNG data serialization to byte array
- [ ] Add compression level configuration

### 5. Base64 Encoding
- [ ] Add base64 encoding utility for binary PNG data
- [ ] Create helper function to convert PNG buffer to base64 string
- [ ] Handle memory-efficient encoding for large images

### 6. HTTP Endpoint Modifications
- [ ] Modify `/api/trace` endpoint to perform ray tracing
- [ ] Return JSON response with base64-encoded PNG data
- [ ] Add optional `format` parameter (png/base64/raw)
- [ ] Include image metadata (width, height, size)

### 7. Direct Download Endpoint
- [ ] Add new GET endpoint `/api/download/:id` for PNG downloads
- [ ] Implement temporary storage mechanism for rendered images
- [ ] Set proper Content-Type headers (image/png)
- [ ] Add Content-Disposition header for browser downloads

### 8. Build Configuration
- [ ] Update BUILD.bazel to include tracy and png_plusplus dependencies
- [ ] Add image_core dependency for RGB types
- [ ] Configure libpng external dependency
- [ ] Set up proper visibility rules

### 9. Error Handling
- [ ] Add validation for scene complexity limits
- [ ] Handle out-of-memory conditions for large images
- [ ] Implement timeout for long-running ray traces
- [ ] Add graceful degradation for unsupported features

### 10. Testing Infrastructure
- [ ] Create unit tests for TracerService
- [ ] Test scene conversion accuracy
- [ ] Verify PNG generation correctness
- [ ] Add integration tests for HTTP endpoints

### 11. Performance Optimization
- [ ] Add multi-threading support for ray tracing
- [ ] Implement progressive rendering with status updates
- [ ] Add caching for frequently rendered scenes
- [ ] Profile and optimize hot paths

### 12. API Documentation
- [ ] Document request/response formats
- [ ] Add example JSON payloads
- [ ] Create OpenAPI/Swagger specification
- [ ] Document performance characteristics

### 13. Client Integration Support
- [ ] Provide JavaScript example for consuming base64 PNG
- [ ] Add CORS headers for web client access
- [ ] Create sample HTML page with download functionality
- [ ] Support streaming for large images

### 14. Monitoring and Logging
- [ ] Add metrics for render time per request
- [ ] Log scene complexity statistics
- [ ] Track PNG generation performance
- [ ] Monitor memory usage during rendering

### 15. Configuration and Tuning
- [ ] Add config for maximum image dimensions
- [ ] Configure ray tracing recursion depth
- [ ] Set memory limits for rendering
- [ ] Add quality vs. performance trade-off settings

## Implementation Order

### Phase 1: Core Implementation
1. Core Ray Tracing Integration
2. Ray Tracing Service Implementation
3. Image Rendering Pipeline
4. Build Configuration

### Phase 2: PNG Export
5. PNG Generation
6. Base64 Encoding
7. HTTP Endpoint Modifications

### Phase 3: Enhanced Features
8. Direct Download Endpoint
9. Error Handling
10. Testing Infrastructure

### Phase 4: Production Readiness
11. Performance Optimization
12. API Documentation
13. Client Integration Support
14. Monitoring and Logging
15. Configuration and Tuning

## Key Files to Modify/Create

### New Files
- `cpp/portrait/tracer_service.h`
- `cpp/portrait/tracer_service.cc`
- `cpp/portrait/tracer_service_test.cc`
- `cpp/portrait/png_utils.h`
- `cpp/portrait/png_utils.cc`
- `cpp/portrait/base64.h`
- `cpp/portrait/base64.cc`

### Modified Files
- `cpp/portrait/BUILD.bazel` - Add new dependencies
- `cpp/portrait/Main.cc` - Update endpoints
- `cpp/portrait/types.h` - Add response types
- `cpp/portrait/types.cc` - Add validation

## Dependencies Required
- `//cpp/tracy` - Ray tracing engine
- `//cpp/png_plusplus` - PNG writing library
- `//cpp/image_core` - Image data structures
- `@libpng` - External PNG library
- `@com_google_absl//absl/strings` - For base64 encoding

## Testing Strategy

### Unit Tests
- Test scene conversion accuracy
- Verify coordinate transformations
- Test PNG generation with various image sizes
- Validate base64 encoding

### Integration Tests
- End-to-end ray tracing with sample scenes
- Verify PNG download functionality
- Test error handling with invalid scenes
- Performance benchmarks

### Manual Testing
- Visual verification of rendered images
- Browser download testing
- Cross-platform compatibility
- Memory leak detection

## Performance Considerations
- Ray tracing is CPU-intensive; consider parallelization
- Large images require significant memory
- Base64 encoding increases payload size by ~33%
- Consider implementing progressive rendering for UX

## Security Considerations
- Validate input dimensions to prevent DoS
- Limit scene complexity (number of objects)
- Implement request rate limiting
- Sanitize file paths for download endpoint