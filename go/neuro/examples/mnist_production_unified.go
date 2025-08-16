package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"strings"
	"time"
	
	"github.com/muchq/moonbase/go/mucks"
	"github.com/muchq/moonbase/go/resilience4g/rate_limit"
	"github.com/muchq/moonbase/go/neuro/inference"
	"github.com/muchq/moonbase/go/neuro/utils"
)

// UnifiedProductionServer supports both CNN and Dense models
type UnifiedProductionServer struct {
	engine    *inference.InferenceEngine
	modelType string // "cnn" or "dense"
	mucks     *mucks.Mucks
	logger    *slog.Logger
}

func main() {
	// Parse command-line flags
	modelPath := flag.String("model", "", "Path to the trained model")
	modelType := flag.String("type", "auto", "Model type: 'cnn', 'dense', or 'auto' (auto-detect)")
	port := flag.Int("port", 8080, "Server port")
	flag.Parse()
	
	// Set up structured logging
	logLevel := slog.LevelInfo
	if os.Getenv("DEBUG") == "true" {
		logLevel = slog.LevelDebug
	}
	
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{
		Level: logLevel,
	}))
	slog.SetDefault(logger)
	
	logger.Info("Unified Production Inference Server starting",
		"service", "mnist-inference-unified",
		"version", "2.0.0")
	
	// Determine model path if not specified
	if *modelPath == "" {
		// Try to find an existing model
		possiblePaths := []string{
			"models/mnist_cnn_final",
			"models/mnist_real_model",
			"models/mnist_cnn_parallel_final",
			"models/mnist_cnn_fixed_final",
		}
		
		for _, path := range possiblePaths {
			if _, err := os.Stat(path); err == nil {
				*modelPath = path
				logger.Info("Auto-detected model", "path", path)
				break
			}
		}
		
		if *modelPath == "" {
			logger.Error("No model found")
			fmt.Println("\nNo trained model found. Please train a model first:")
			fmt.Println("  For dense network: bazel run //go/neuro/examples:mnist_real")
			fmt.Println("  For CNN: bazel run //go/neuro/examples:mnist_cnn_full")
			fmt.Println("\nOr specify a model path:")
			fmt.Println("  bazel run //go/neuro/examples:mnist_production_unified -- -model=path/to/model")
			os.Exit(1)
		}
	}
	
	// Load model
	logger.Info("Loading model", "path", *modelPath)
	engine, err := inference.LoadModelForInference(*modelPath)
	if err != nil {
		logger.Error("Failed to load model", "error", err, "path", *modelPath)
		os.Exit(1)
	}
	
	// Auto-detect model type if needed
	if *modelType == "auto" {
		*modelType = detectModelType(*modelPath, engine)
		logger.Info("Auto-detected model type", "type", *modelType)
	}
	
	logger.Info("Model loaded successfully", "type", *modelType)
	
	// Warmup for consistent latency
	logger.Info("Warming up model", "iterations", 10)
	if err := engine.Warmup(10); err != nil {
		logger.Error("Warmup failed", "error", err)
	}
	
	// Start production server
	server := &UnifiedProductionServer{
		engine:    engine,
		modelType: *modelType,
		logger:    logger,
	}
	server.Start(*port)
}

func detectModelType(modelPath string, engine *inference.InferenceEngine) string {
	// Check based on path name
	if strings.Contains(modelPath, "cnn") {
		return "cnn"
	}
	
	// Check based on model info
	info := engine.GetModelInfo()
	
	// CNN models typically have Conv2D layers
	for _, layer := range info.Layers {
		if strings.Contains(strings.ToLower(layer.Type), "conv") {
			return "cnn"
		}
	}
	
	// Default to dense
	return "dense"
}

func (s *UnifiedProductionServer) Start(port int) {
	// Create mucks router with JSON middleware
	s.mucks = mucks.NewJsonMucks()
	
	// Configure rate limiting
	rateLimitConfig := &rate_limit.DefaultRateLimitConfig{
		MaxTokens:  100,
		RefillRate: 100,
		OpCost:     1,
	}
	
	factory := &rate_limit.TokenBucketRateLimiterFactory{}
	
	// Add rate limiting middleware
	rateLimiter := rate_limit.NewRateLimiterMiddleware(
		factory,
		rate_limit.RemoteIpKeyExtractor{},
		rateLimitConfig,
	)
	s.mucks.Add(rateLimiter)
	
	// Add logging middleware
	s.mucks.Add(&LoggingMiddleware{Logger: s.logger})
	
	// Register routes
	s.mucks.HandleFunc("POST /predict", s.handlePredict)
	s.mucks.HandleFunc("GET /health", s.handleHealth)
	s.mucks.HandleFunc("GET /info", s.handleInfo)
	s.mucks.HandleFunc("GET /", s.handleRoot)
	
	s.logger.Info("Server configuration",
		"port", port,
		"model_type", s.modelType,
		"rate_limit", "100 req/s per IP",
		"logging", "structured JSON")
	
	s.logger.Info("Endpoints registered",
		"endpoints", []string{
			"POST /predict",
			"GET /health",
			"GET /info",
			"GET /",
		})
	
	fmt.Printf("\n🚀 Unified Production Server running on http://localhost:%d\n", port)
	fmt.Printf("📊 Model Type: %s\n", s.modelType)
	fmt.Printf("📝 Structured logging enabled (JSON format)\n")
	fmt.Printf("\nTest with: curl -X POST http://localhost:%d/predict -H 'Content-Type: application/json' -d '{\"pixels\":[...]}'\n", port)
	fmt.Printf("Press Ctrl+C to stop the server\n")
	
	addr := fmt.Sprintf(":%d", port)
	if err := http.ListenAndServe(addr, s.mucks); err != nil {
		s.logger.Error("Server failed", "error", err)
		os.Exit(1)
	}
}

func (s *UnifiedProductionServer) handlePredict(w http.ResponseWriter, r *http.Request) {
	ctx := r.Context()
	
	// Parse input
	var request PredictRequest
	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		s.logger.WarnContext(ctx, "Invalid request body", "error", err)
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusBadRequest,
			ErrorCode:  400,
			Message:    "Invalid request body",
			Detail:     err.Error(),
		})
		return
	}
	
	// Validate input
	if len(request.Pixels) != 784 {
		s.logger.WarnContext(ctx, "Invalid input size", "size", len(request.Pixels))
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusBadRequest,
			ErrorCode:  400,
			Message:    "Invalid input size",
			Detail:     "Input must be 784 pixels (28x28 image)",
		})
		return
	}
	
	// Convert to tensor based on model type
	var input *utils.Tensor
	if s.modelType == "cnn" {
		// CNN expects 4D input: (batch, channels, height, width)
		input = utils.NewTensor(1, 1, 28, 28)
		for i, pixel := range request.Pixels {
			input.Data[i] = pixel
		}
	} else {
		// Dense network expects 2D input: (batch, features)
		input = utils.NewTensor(1, 784)
		for i, pixel := range request.Pixels {
			input.Set(pixel, 0, i)
		}
	}
	
	// Make prediction
	start := time.Now()
	classIdx, confidence, err := s.engine.PredictClass(input)
	latency := time.Since(start)
	
	if err != nil {
		s.logger.ErrorContext(ctx, "Prediction failed", "error", err)
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusInternalServerError,
			ErrorCode:  500,
			Message:    "Prediction failed",
			Detail:     "Internal model error",
		})
		return
	}
	
	// Get top 3 predictions
	indices, values, _ := s.engine.PredictTopK(input, 3)
	
	// Prepare response
	response := PredictResponse{
		PredictedClass: classIdx,
		Confidence:     confidence,
		ModelType:      s.modelType,
		TopPredictions: make([]ClassPrediction, len(indices)),
		LatencyMs:      float64(latency.Microseconds()) / 1000.0,
	}
	
	for i := range indices {
		response.TopPredictions[i] = ClassPrediction{
			Class:      indices[i],
			Confidence: values[i],
		}
	}
	
	s.logger.InfoContext(ctx, "Prediction successful",
		"predicted_class", classIdx,
		"confidence", confidence,
		"model_type", s.modelType,
		"latency_ms", response.LatencyMs)
	
	// Send response
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (s *UnifiedProductionServer) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status": "healthy",
		"service": "mnist-inference-unified",
		"model_type": s.modelType,
	})
}

func (s *UnifiedProductionServer) handleInfo(w http.ResponseWriter, r *http.Request) {
	info := s.engine.GetModelInfo()
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"model_type":   s.modelType,
		"input_shape":  info.InputShape,
		"output_shape": info.OutputShape,
		"classes":      info.Classes,
		"layers":       len(info.Layers),
		"layer_details": info.Layers,
		"note":         "Unified server supporting both CNN and Dense models",
	})
}

func (s *UnifiedProductionServer) handleRoot(w http.ResponseWriter, r *http.Request) {
	html := fmt.Sprintf(`
<!DOCTYPE html>
<html>
<head>
    <title>MNIST Unified Inference Server</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        h1 { color: #333; }
        .info { background: #f0f0f0; padding: 20px; border-radius: 5px; margin: 20px 0; }
        .endpoint { margin: 10px 0; padding: 10px; background: white; border-left: 3px solid #4CAF50; }
        code { background: #f4f4f4; padding: 2px 5px; border-radius: 3px; }
        .model-type { font-size: 1.2em; color: #2196F3; font-weight: bold; }
    </style>
</head>
<body>
    <h1>🚀 MNIST Unified Inference Server</h1>
    <div class="info">
        <p>Model Type: <span class="model-type">%s</span></p>
        <p>This server automatically handles both CNN and Dense network models.</p>
    </div>
    
    <h2>Available Endpoints:</h2>
    
    <div class="endpoint">
        <strong>POST /predict</strong>
        <p>Make a prediction on a 28x28 grayscale image</p>
        <p>Request body:</p>
        <code>{"pixels": [784 float values between 0 and 1]}</code>
    </div>
    
    <div class="endpoint">
        <strong>GET /health</strong>
        <p>Check server health status</p>
    </div>
    
    <div class="endpoint">
        <strong>GET /info</strong>
        <p>Get model information and layer details</p>
    </div>
    
    <h2>Example Usage:</h2>
    <pre><code>curl -X POST http://localhost:8080/predict \
  -H 'Content-Type: application/json' \
  -d '{"pixels": [0.0, 0.0, ...]}'</code></pre>
    
    <div class="info">
        <p><strong>Note:</strong> The server automatically detects whether the loaded model is a CNN or Dense network and adjusts the input tensor shape accordingly.</p>
    </div>
</body>
</html>
`, s.modelType)
	
	w.Header().Set("Content-Type", "text/html")
	w.Write([]byte(html))
}

// Request/Response types
type PredictRequest struct {
	Pixels []float64 `json:"pixels"`
}

type PredictResponse struct {
	PredictedClass int                `json:"predicted_class"`
	Confidence     float64            `json:"confidence"`
	ModelType      string             `json:"model_type"`
	TopPredictions []ClassPrediction  `json:"top_predictions"`
	LatencyMs      float64            `json:"latency_ms"`
}

type ClassPrediction struct {
	Class      int     `json:"class"`
	Confidence float64 `json:"confidence"`
}

// LoggingMiddleware implements mucks.Middleware for request logging
type LoggingMiddleware struct {
	Logger *slog.Logger
}

func (l *LoggingMiddleware) Wrap(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		
		// Create a response writer wrapper to capture status code
		wrapped := &responseWriter{ResponseWriter: w, statusCode: http.StatusOK}
		
		// Log request
		l.Logger.Info("Request received",
			"method", r.Method,
			"path", r.URL.Path,
			"remote_addr", r.RemoteAddr,
			"user_agent", r.UserAgent())
		
		// Process request
		next(wrapped, r)
		
		// Log response
		duration := time.Since(start)
		l.Logger.Info("Request completed",
			"method", r.Method,
			"path", r.URL.Path,
			"status", wrapped.statusCode,
			"duration_ms", float64(duration.Microseconds())/1000.0)
	}
}

type responseWriter struct {
	http.ResponseWriter
	statusCode int
}

func (rw *responseWriter) WriteHeader(code int) {
	rw.statusCode = code
	rw.ResponseWriter.WriteHeader(code)
}