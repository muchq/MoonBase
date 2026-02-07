package main

import (
	"encoding/json"
	"fmt"
	"image"
	"image/color"
	"io"
	"log/slog"
	"net/http"
	"os"
	"time"

	"github.com/muchq/moonbase/domains/platform/libs/mucks"
	"github.com/muchq/moonbase/domains/platform/libs/resilience4g/rate_limit"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/inference"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
)

// CNNProductionServer demonstrates a production inference service for CNN models
type CNNProductionServer struct {
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
	
	logger.Info("CNN Production Inference Server starting",
		"service", "mnist-cnn-inference",
		"version", "1.0.0")
	
	// Look for the CNN model
	modelPath := "models/mnist_cnn_final"
	
	logger.Info("Loading CNN model", "path", modelPath)
	engine, err := inference.LoadModelForInference(modelPath)
	if err != nil {
		logger.Error("CNN model not found", "error", err)
		fmt.Println("Please run mnist_cnn_full example first to train a CNN model.")
		fmt.Println("Run: bazel run //domains/ai/libs/neuro/examples:mnist_cnn_full")
		
		// For demo purposes, create a mock server
		logger.Info("Starting mock server for demonstration")
		startMockServer()
		return
	}
	
	logger.Info("CNN model loaded successfully")
	
	// Warmup for consistent latency
	logger.Info("Warming up model", "iterations", 20)
	if err := engine.Warmup(20); err != nil {
		logger.Error("Warmup failed", "error", err)
	}
	
	// Start production server
	server := &CNNProductionServer{
		engine: engine,
		logger: logger,
	}
	server.Start()
}

func (s *CNNProductionServer) Start() {
	// Create mucks router with JSON middleware
	s.mucks = mucks.NewJsonMucks()
	
	// Configure rate limiting
	// Allow 200 requests per second per IP address (higher for CNN)
	rateLimitConfig := &rate_limit.DefaultRateLimitConfig{
		MaxTokens:  200,  // Maximum tokens in bucket
		RefillRate: 200,  // Tokens per second
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
	s.mucks.HandleFunc("POST /predict-image", s.handlePredictImage)
	s.mucks.HandleFunc("GET /health", s.handleHealth)
	s.mucks.HandleFunc("GET /info", s.handleInfo)
	s.mucks.HandleFunc("GET /demo", s.handleDemo)
	
	s.logger.Info("Server configuration",
		"port", 8081,
		"rate_limit", "200 req/s per IP",
		"logging", "structured JSON",
		"middleware", []string{"rate_limiter", "logger", "json"})
	
	s.logger.Info("Endpoints registered",
		"endpoints", []string{
			"POST /predict",
			"POST /predict-image",
			"GET /health",
			"GET /info",
			"GET /demo",
		})
	
	fmt.Println("\n🚀 CNN Production server running on http://localhost:8081")
	fmt.Println("📊 Structured logging enabled (JSON format)")
	fmt.Println("\nTry the demo page: http://localhost:8081/demo")
	fmt.Println("Press Ctrl+C to stop the server")
	
	if err := http.ListenAndServe(":8081", s.mucks); err != nil {
		s.logger.Error("Server failed", "error", err)
		os.Exit(1)
	}
}

func (s *CNNProductionServer) handlePredict(w http.ResponseWriter, r *http.Request) {
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
	
	// Convert to tensor with CNN input shape (batch=1, channels=1, height=28, width=28)
	input := utils.NewTensor(1, 1, 28, 28)
	for i, pixel := range request.Pixels {
		input.Data[i] = pixel
	}
	
	// Make prediction
	start := time.Now()
	output, err := s.engine.Predict(input)
	if err != nil {
		s.logger.ErrorContext(ctx, "CNN prediction failed", "error", err)
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusInternalServerError,
			ErrorCode:  500,
			Message:    "Prediction failed",
			Detail:     "Internal model error",
		})
		return
	}
	
	// Extract predictions from output
	predictions := make([]float64, 10)
	copy(predictions, output.Data[:10])
	
	// Find the predicted class
	classIdx := 0
	confidence := predictions[0]
	for i := 1; i < 10; i++ {
		if predictions[i] > confidence {
			confidence = predictions[i]
			classIdx = i
		}
	}
	
	latency := time.Since(start)
	
	// Prepare response
	response := PredictResponse{
		PredictedClass: classIdx,
		Confidence:     confidence,
		AllPredictions: predictions,
		LatencyMs:      float64(latency.Microseconds()) / 1000.0,
		ModelType:      "CNN",
	}
	
	s.logger.InfoContext(ctx, "CNN prediction successful",
		"predicted_class", classIdx,
		"confidence", confidence,
		"latency_ms", response.LatencyMs)
	
	// Send response
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (s *CNNProductionServer) handlePredictImage(w http.ResponseWriter, r *http.Request) {
	ctx := r.Context()
	
	// Parse multipart form (max 10MB)
	err := r.ParseMultipartForm(10 << 20)
	if err != nil {
		s.logger.WarnContext(ctx, "Failed to parse form", "error", err)
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusBadRequest,
			ErrorCode:  400,
			Message:    "Failed to parse form",
			Detail:     err.Error(),
		})
		return
	}
	
	// Get the file
	file, _, err := r.FormFile("image")
	if err != nil {
		s.logger.WarnContext(ctx, "Failed to get image file", "error", err)
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusBadRequest,
			ErrorCode:  400,
			Message:    "Failed to get image file",
			Detail:     "Please upload an image with field name 'image'",
		})
		return
	}
	defer file.Close()
	
	// Decode image
	img, _, err := image.Decode(file)
	if err != nil {
		s.logger.WarnContext(ctx, "Failed to decode image", "error", err)
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusBadRequest,
			ErrorCode:  400,
			Message:    "Failed to decode image",
			Detail:     "Please upload a valid image file (PNG, JPEG, etc.)",
		})
		return
	}
	
	// Convert to grayscale and resize to 28x28
	pixels := preprocessImage(img)
	
	// Convert to tensor with CNN input shape
	input := utils.NewTensor(1, 1, 28, 28)
	copy(input.Data, pixels)
	
	// Make prediction
	start := time.Now()
	output, err := s.engine.Predict(input)
	if err != nil {
		s.logger.ErrorContext(ctx, "CNN image prediction failed", "error", err)
		mucks.JsonError(w, mucks.Problem{
			StatusCode: http.StatusInternalServerError,
			ErrorCode:  500,
			Message:    "Prediction failed",
			Detail:     "Internal model error",
		})
		return
	}
	
	// Extract predictions
	predictions := make([]float64, 10)
	copy(predictions, output.Data[:10])
	
	// Find the predicted class
	classIdx := 0
	confidence := predictions[0]
	for i := 1; i < 10; i++ {
		if predictions[i] > confidence {
			confidence = predictions[i]
			classIdx = i
		}
	}
	
	latency := time.Since(start)
	
	// Prepare response
	response := PredictResponse{
		PredictedClass: classIdx,
		Confidence:     confidence,
		AllPredictions: predictions,
		LatencyMs:      float64(latency.Microseconds()) / 1000.0,
		ModelType:      "CNN",
	}
	
	s.logger.InfoContext(ctx, "CNN image prediction successful",
		"predicted_class", classIdx,
		"confidence", confidence,
		"latency_ms", response.LatencyMs)
	
	// Send response
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (s *CNNProductionServer) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status":  "healthy",
		"service": "mnist-cnn-inference",
		"model":   "CNN",
	})
}

func (s *CNNProductionServer) handleInfo(w http.ResponseWriter, r *http.Request) {
	info := s.engine.GetModelInfo()
	
	// Count parameters in CNN model
	totalParams := 0
	for _, layer := range info.Layers {
		if layer.Type == "Conv2D" {
			// Estimate parameters for Conv2D
			params := layer.Params
			if inCh, ok1 := params["in_channels"].(float64); ok1 {
				if outCh, ok2 := params["out_channels"].(float64); ok2 {
					if kernelSize, ok3 := params["kernel_size"].([]interface{}); ok3 && len(kernelSize) == 2 {
						if kh, ok4 := kernelSize[0].(float64); ok4 {
							if kw, ok5 := kernelSize[1].(float64); ok5 {
								// weights + bias
								totalParams += int(inCh * outCh * kh * kw)
								if useBias, ok6 := params["use_bias"].(bool); ok6 && useBias {
									totalParams += int(outCh)
								}
							}
						}
					}
				}
			}
		} else if layer.Type == "Dense" {
			// Count parameters for Dense layers
			params := layer.Params
			if inSize, ok1 := params["input_size"].(float64); ok1 {
				if outSize, ok2 := params["output_size"].(float64); ok2 {
					totalParams += int(inSize*outSize + outSize) // weights + bias
				}
			}
			}
		}
	
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"model_type":        "CNN",
		"architecture":      "2x Conv2D + MaxPool2D + 2x Dense",
		"input_shape":       []int{1, 1, 28, 28},
		"output_shape":      []int{1, 10},
		"classes":           10,
		"layers":            len(info.Layers),
		"total_parameters":  totalParams,
		"note":              "Convolutional Neural Network optimized for MNIST digit recognition",
		"performance_notes": "Lower latency than fully connected model on GPU, better accuracy",
	})
}

func (s *CNNProductionServer) handleDemo(w http.ResponseWriter, r *http.Request) {
	html := `<!DOCTYPE html>
<html>
<head>
	<title>MNIST CNN Demo</title>
	<style>
		body { font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }
		h1 { color: #333; }
		canvas { border: 2px solid #333; background: white; cursor: crosshair; margin: 20px 0; }
		button { padding: 10px 20px; margin: 5px; font-size: 16px; cursor: pointer; }
		button:hover { background: #e0e0e0; }
		#result { margin: 20px 0; padding: 20px; background: white; border-radius: 5px; }
		.prediction { display: inline-block; margin: 5px; padding: 5px 10px; background: #f0f0f0; }
		.confidence-bar { height: 20px; background: linear-gradient(to right, #4CAF50, #8BC34A); margin: 2px 0; }
		.stats { color: #666; font-size: 14px; margin-top: 10px; }
	</style>
</head>
<body>
	<h1>MNIST CNN Digit Recognition Demo</h1>
	<p>Draw a digit (0-9) on the canvas below:</p>
	<canvas id="canvas" width="280" height="280"></canvas>
	<br>
	<button onclick="predict()">Predict</button>
	<button onclick="clearCanvas()">Clear</button>
	<div id="result"></div>
	
	<script>
		const canvas = document.getElementById('canvas');
		const ctx = canvas.getContext('2d');
		let drawing = false;
		
		ctx.lineWidth = 15;
		ctx.lineCap = 'round';
		ctx.strokeStyle = '#000';
		
		canvas.addEventListener('mousedown', (e) => {
			drawing = true;
			ctx.beginPath();
			ctx.moveTo(e.offsetX, e.offsetY);
		});
		
		canvas.addEventListener('mousemove', (e) => {
			if (drawing) {
				ctx.lineTo(e.offsetX, e.offsetY);
				ctx.stroke();
			}
		});
		
		canvas.addEventListener('mouseup', () => drawing = false);
		canvas.addEventListener('mouseout', () => drawing = false);
		
		// Touch events for mobile
		canvas.addEventListener('touchstart', (e) => {
			drawing = true;
			const rect = canvas.getBoundingClientRect();
			const touch = e.touches[0];
			const x = touch.clientX - rect.left;
			const y = touch.clientY - rect.top;
			ctx.beginPath();
			ctx.moveTo(x, y);
			e.preventDefault();
		});
		
		canvas.addEventListener('touchmove', (e) => {
			if (drawing) {
				const rect = canvas.getBoundingClientRect();
				const touch = e.touches[0];
				const x = touch.clientX - rect.left;
				const y = touch.clientY - rect.top;
				ctx.lineTo(x, y);
				ctx.stroke();
				e.preventDefault();
			}
		});
		
		canvas.addEventListener('touchend', () => drawing = false);
		
		function clearCanvas() {
			ctx.clearRect(0, 0, canvas.width, canvas.height);
			document.getElementById('result').innerHTML = '';
		}
		
		async function predict() {
			// Get image data and downsample to 28x28
			const imageData = ctx.getImageData(0, 0, 280, 280);
			const pixels = [];
			
			// Downsample by taking every 10th pixel
			for (let y = 0; y < 28; y++) {
				for (let x = 0; x < 28; x++) {
					const srcX = x * 10;
					const srcY = y * 10;
					const idx = (srcY * 280 + srcX) * 4;
					// Convert to grayscale and normalize
					const gray = (imageData.data[idx] + imageData.data[idx+1] + imageData.data[idx+2]) / 3;
					pixels.push((255 - gray) / 255); // Invert for MNIST format
				}
			}
			
			try {
				const response = await fetch('/predict', {
					method: 'POST',
					headers: { 'Content-Type': 'application/json' },
					body: JSON.stringify({ pixels })
				});
				
				const data = await response.json();
				
				if (data.predicted_class !== undefined) {
					let html = '<h2>CNN Prediction: <strong>' + data.predicted_class + '</strong></h2>';
					html += '<div class="stats">Model: CNN | Latency: ' + data.latency_ms.toFixed(2) + 'ms</div>';
					html += '<h3>Confidence Scores:</h3>';
					
					for (let i = 0; i < 10; i++) {
						const conf = (data.all_predictions[i] * 100).toFixed(1);
						const barWidth = conf + '%';
						html += '<div class="prediction">';
						html += 'Digit ' + i + ': ' + conf + '%';
						html += '<div class="confidence-bar" style="width: ' + barWidth + '"></div>';
						html += '</div>';
					}
					
					document.getElementById('result').innerHTML = html;
				} else {
					document.getElementById('result').innerHTML = '<p>Error: ' + JSON.stringify(data) + '</p>';
				}
			} catch (error) {
				document.getElementById('result').innerHTML = '<p>Error: ' + error + '</p>';
			}
		}
	</script>
</body>
</html>`
	
	w.Header().Set("Content-Type", "text/html")
	w.Write([]byte(html))
}

// preprocessImage converts an image to 28x28 grayscale for MNIST
func preprocessImage(img image.Image) []float64 {
	bounds := img.Bounds()
	width, height := bounds.Max.X, bounds.Max.Y
	
	// Create 28x28 array
	pixels := make([]float64, 784)
	
	// Simple nearest-neighbor resizing
	for y := 0; y < 28; y++ {
		for x := 0; x < 28; x++ {
			// Map to original image coordinates
			srcX := x * width / 28
			srcY := y * height / 28
			
			// Get pixel and convert to grayscale
			c := img.At(srcX, srcY)
			gray := color.GrayModel.Convert(c).(color.Gray)
			
			// Normalize to 0-1 range (invert for MNIST)
			pixels[y*28+x] = float64(255-gray.Y) / 255.0
		}
	}
	
	return pixels
}

// Request/Response types
type PredictRequest struct {
	Pixels []float64 `json:"pixels"`
}

type PredictResponse struct {
	PredictedClass int       `json:"predicted_class"`
	Confidence     float64   `json:"confidence"`
	AllPredictions []float64 `json:"all_predictions"`
	LatencyMs      float64   `json:"latency_ms"`
	ModelType      string    `json:"model_type"`
}

// LoggingMiddleware logs all incoming requests
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
	
	// Add basic rate limiting
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
			Message:    "CNN model not loaded",
			Detail:     "Run mnist_cnn_full example first to train a CNN model",
		})
	})
	
	m.HandleFunc("GET /health", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{
			"status": "mock",
			"model":  "CNN",
		})
	})
	
	m.HandleFunc("GET /demo", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html")
		io.WriteString(w, "<h1>CNN Model Not Available</h1><p>Please train the CNN model first by running: bazel run //domains/ai/libs/neuro/examples:mnist_cnn_full</p>")
	})
	
	logger.Info("Mock CNN server starting",
		"port", 8081,
		"rate_limit", "10 req/s per IP",
		"mode", "mock")
	
	fmt.Println("\n🔧 Mock CNN server running on http://localhost:8081")
	fmt.Println("📊 Structured logging enabled (JSON format)")
	
	if err := http.ListenAndServe(":8081", m); err != nil {
		logger.Error("Mock server failed", "error", err)
		os.Exit(1)
	}
}