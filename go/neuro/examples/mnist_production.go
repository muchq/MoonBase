package main

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"time"
	
	"github.com/muchq/moonbase/go/mucks"
	"github.com/muchq/moonbase/go/resilience4g/rate_limit"
	"github.com/MoonBase/go/neuro/inference"
	"github.com/MoonBase/go/neuro/utils"
)

// ProductionServer demonstrates a proper production inference service
// that ONLY loads the trained model, not the training data
type ProductionServer struct {
	engine *inference.InferenceEngine
	mucks  *mucks.Mucks
	logger *slog.Logger
}

func main() {
	// Set up structured logging
	logLevel := slog.LevelInfo
	if os.Getenv("DEBUG") == "true" {
		logLevel = slog.LevelDebug
	}
	
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{
		Level: logLevel,
	}))
	slog.SetDefault(logger)
	
	logger.Info("Production Inference Server starting",
		"service", "mnist-inference",
		"version", "1.0.0")
	
	// In production, you would load a model trained elsewhere
	// For this demo, we'll use a model if it exists
	modelPath := "models/mnist_vendored_model"
	
	logger.Info("Loading model", "path", modelPath)
	engine, err := inference.LoadModelForInference(modelPath)
	if err != nil {
		logger.Error("Model not found", "error", err)
		fmt.Println("Please run mnist_vendored example first to train a model.")
		fmt.Println("Run: bazel run //go/neuro/examples:mnist_vendored")
		
		// For demo purposes, create a mock server
		logger.Info("Starting mock server for demonstration")
		startMockServer()
		return
	}
	
	logger.Info("Model loaded successfully")
	
	// Warmup for consistent latency
	logger.Info("Warming up model", "iterations", 10)
	if err := engine.Warmup(10); err != nil {
		logger.Error("Warmup failed", "error", err)
	}
	
	// Start production server
	server := &ProductionServer{
		engine: engine,
		logger: logger,
	}
	server.Start()
}

func (s *ProductionServer) Start() {
	// Create mucks router with JSON middleware
	s.mucks = mucks.NewJsonMucks()
	
	// Configure rate limiting
	// Allow 100 requests per second per IP address
	rateLimitConfig := &rate_limit.DefaultRateLimitConfig{
		MaxTokens:  100,  // Maximum tokens in bucket
		RefillRate: 100,  // Tokens per second
		OpCost:     1,    // Cost per request
	}
	
	// Create rate limiter factory
	factory := &rate_limit.TokenBucketRateLimiterFactory{}
	
	// Add rate limiting middleware (per IP address)
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
	
	s.logger.Info("Server configuration",
		"port", 8080,
		"rate_limit", "100 req/s per IP",
		"logging", "structured JSON",
		"middleware", []string{"rate_limiter", "logger", "json"})
	
	s.logger.Info("Endpoints registered",
		"endpoints", []string{
			"POST /predict",
			"GET /health",
			"GET /info",
		})
	
	fmt.Println("\n🚀 Production server running on http://localhost:8080")
	fmt.Println("📊 Structured logging enabled (JSON format)")
	fmt.Println("Press Ctrl+C to stop the server")
	
	if err := http.ListenAndServe(":8080", s.mucks); err != nil {
		s.logger.Error("Server failed", "error", err)
		os.Exit(1)
	}
}

func (s *ProductionServer) handlePredict(w http.ResponseWriter, r *http.Request) {
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
	
	// Convert to tensor
	input := utils.NewTensor(1, 784)
	for i, pixel := range request.Pixels {
		input.Set(pixel, 0, i)
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
		"latency_ms", response.LatencyMs)
	
	// Send response
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (s *ProductionServer) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status": "healthy",
		"service": "mnist-inference",
	})
}

func (s *ProductionServer) handleInfo(w http.ResponseWriter, r *http.Request) {
	info := s.engine.GetModelInfo()
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"model_type":   info.ModelType,
		"input_shape":  info.InputShape,
		"output_shape": info.OutputShape,
		"classes":      info.Classes,
		"layers":       len(info.Layers),
		"note":         "This server contains ONLY the model weights, not training data",
	})
}

// Request/Response types
type PredictRequest struct {
	Pixels []float64 `json:"pixels"`
}

type PredictResponse struct {
	PredictedClass int                `json:"predicted_class"`
	Confidence     float64            `json:"confidence"`
	TopPredictions []ClassPrediction  `json:"top_predictions"`
	LatencyMs      float64            `json:"latency_ms"`
}

type ClassPrediction struct {
	Class      int     `json:"class"`
	Confidence float64 `json:"confidence"`
}

// LoggingMiddleware logs all incoming requests with structured logging
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
		
		next(wrapped, r)
		
		// Log response
		l.Logger.Info("Request completed",
			"method", r.Method,
			"path", r.URL.Path,
			"status", wrapped.statusCode,
			"duration_ms", time.Since(start).Milliseconds())
	}
}

// responseWriter wraps http.ResponseWriter to capture status code
type responseWriter struct {
	http.ResponseWriter
	statusCode int
}

func (rw *responseWriter) WriteHeader(code int) {
	rw.statusCode = code
	rw.ResponseWriter.WriteHeader(code)
}

// Mock server for when model doesn't exist
func startMockServer() {
	logger := slog.Default()
	m := mucks.NewJsonMucks()
	
	// Add basic rate limiting for the mock server too
	rateLimitConfig := &rate_limit.DefaultRateLimitConfig{
		MaxTokens:  10,
		RefillRate: 10,
		OpCost:     1,
	}
	
	factory := &rate_limit.TokenBucketRateLimiterFactory{}
	
	rateLimiter := rate_limit.NewRateLimiterMiddleware(
		factory,
		rate_limit.RemoteIpKeyExtractor{},
		rateLimitConfig,
	)
	m.Add(rateLimiter)
	m.Add(&LoggingMiddleware{Logger: logger})
	
	m.HandleFunc("POST /predict", func(w http.ResponseWriter, r *http.Request) {
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusServiceUnavailable,
			ErrorCode:  503,
			Message:    "Model not loaded",
			Detail:     "Run mnist_vendored example first to train a model",
		})
	})
	
	m.HandleFunc("GET /health", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{
			"status": "mock",
		})
	})
	
	logger.Info("Mock server starting",
		"port", 8080,
		"rate_limit", "10 req/s per IP",
		"mode", "mock")
	
	fmt.Println("\n🔧 Mock server running on http://localhost:8080")
	fmt.Println("📊 Structured logging enabled (JSON format)")
	
	if err := http.ListenAndServe(":8080", m); err != nil {
		logger.Error("Mock server failed", "error", err)
		os.Exit(1)
	}
}