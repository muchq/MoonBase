# Neuro Examples

This directory contains example applications demonstrating the Neuro neural network framework.

## Examples

### 1. mnist_example.go - Synthetic Data Demo
A complete end-to-end demonstration using synthetic data that:
- Shows the full workflow from training to inference
- Uses randomly generated data for quick testing
- Demonstrates all inference features (batch processing, warmup, etc.)
- Runs quickly without external dependencies

**Run it:**
```bash
bazel run //domains/ai/libs/neuro/examples:mnist_example
```

### 2. mnist_real.go - Real MNIST Dataset
A production-ready example using the actual MNIST handwritten digit dataset:
- Uses vendored MNIST data (no download required)
- Trains a neural network to recognize handwritten digits
- Achieves good accuracy on real-world data
- Demonstrates practical deployment scenarios

**Run it:**
```bash
bazel run //domains/ai/libs/neuro/examples:mnist_real
```

### 3. mnist_vendored.go - Vendored MNIST Dataset
Demonstrates using embedded MNIST data that compiles into the binary:
- MNIST data is embedded at compile time
- No external files or downloads needed
- Binary is self-contained with all data
- Ideal for training deployments

**Run it:**
```bash
bazel run //domains/ai/libs/neuro/examples:mnist_vendored
```

### 4. mnist_production.go - Production Inference Server
**Demonstrates proper production deployment** without training data:
- Loads ONLY the trained model (no dataset embedding)
- Uses **mucks** for HTTP routing and middleware
- Uses **resilience4g** for rate limiting (100 req/s per IP)
- Uses **slog** for structured JSON logging
- Proper error handling with JSON responses
- Shows the correct pattern for production services

**Features:**
- ✅ **Structured Logging** with slog (JSON format)
- ✅ Rate limiting with token bucket algorithm
- ✅ Per-IP address tracking
- ✅ JSON error responses with mucks.Problem
- ✅ Request/response logging with latency metrics
- ✅ Context propagation for tracing
- ✅ Health check endpoint
- ✅ Model info endpoint

**Run it:**
```bash
# First train a model
bazel run //domains/ai/libs/neuro/examples:mnist_vendored

# Then start the production server (runs until Ctrl+C)
bazel run //domains/ai/libs/neuro/examples:mnist_production

# In another terminal, test the API:
curl http://localhost:8080/health
curl http://localhost:8080/info
curl -X POST http://localhost:8080/predict \
  -H 'Content-Type: application/json' \
  -d '{"pixels": [0.0, 0.1, ...]}'  # 784 values
```

**Testing rate limiting:**
```bash
# Send 150 requests quickly (limit is 100/sec)
for i in {1..150}; do 
  curl -s http://localhost:8080/health > /dev/null & 
done
# Check server logs - some requests will be rate limited
```

## Understanding MNIST

MNIST is a classic dataset in machine learning containing:
- **60,000 training images** and **10,000 test images**
- Each image is **28x28 pixels** (784 features when flattened)
- **10 classes** representing digits 0-9
- Real handwritten digits from various writers

The dataset is automatically downloaded and cached locally when you run the `mnist_real` example.

## What the Examples Demonstrate

### Training Phase
1. **Data Loading**: Loading and preprocessing image data
2. **Normalization**: Scaling pixel values and standardizing features
3. **Model Architecture**: Building a multi-layer neural network
4. **Training Loop**: Iterating through epochs with batch processing
5. **Evaluation**: Testing accuracy on held-out test data

### Inference Phase
1. **Model Serialization**: Saving trained models to disk
2. **Model Loading**: Loading models for production use
3. **Single Prediction**: Classifying individual images
4. **Batch Processing**: Efficiently processing multiple images
5. **Confidence Scores**: Getting probability distributions over classes
6. **Top-K Predictions**: Getting multiple likely predictions

### Key Differences

| Aspect | mnist_example | mnist_real | mnist_vendored | mnist_production |
|--------|--------------|------------|----------------|------------------|
| Data Source | Synthetic | Vendored MNIST | Vendored MNIST | None (model only) |
| Dataset in Binary | No | Yes (11MB) | Yes (11MB) | No |
| Model in Binary | No | No | No | Yes (~1MB) |
| Training | Yes | Yes | Yes | No |
| Inference | Demo | Demo | Demo | Production |
| Use Case | Testing | Development | Training | **Production** |

## Deployment Patterns

### ⚠️ Important: Binary Size Considerations

**For Training Applications:**
```go
// OK to embed datasets for training convenience
//go:embed mnist_vendor/*.gz
var mnistFiles embed.FS
```
Binary size: ~11MB+ (includes full dataset)

**For Production Inference:**
```go
// ONLY load the trained model
engine, _ := inference.LoadModelForInference("model.json")
```
Binary size: ~1MB (just model weights)

### Why This Matters

1. **Production servers** handle thousands of requests - they need to be lean
2. **Training data** is only needed during model development
3. **Model files** are 10-100x smaller than datasets
4. **Container images** should be minimal for fast deployment
5. **Memory usage** is critical in production environments

## Production Architecture

The `mnist_production` example demonstrates production best practices:

### Middleware Stack (using mucks)
```
Request → Rate Limiter → Logger → JSON Handler → Route Handler → Response
```

### Rate Limiting (using resilience4g)
- **Algorithm**: Token bucket
- **Scope**: Per IP address
- **Default**: 100 requests/second
- **Configurable**: MaxTokens, RefillRate, OpCost

### Benefits of This Architecture
1. **Protection**: Rate limiting prevents abuse
2. **Observability**: Structured logging for monitoring and debugging
3. **Maintainability**: Clean middleware separation
4. **Scalability**: Stateless design, ready for horizontal scaling
5. **Security**: Per-IP tracking, JSON error responses

### Why Structured Logging (slog)?

The production example uses Go's `slog` package instead of `fmt.Println` or `log.Printf`:

**Traditional logging (bad for production):**
```go
log.Printf("[%s] %s %s", r.Method, r.URL.Path, r.RemoteAddr)
// Output: [GET] /health 127.0.0.1:54321
```

**Structured logging with slog (production-ready):**
```go
logger.Info("Request received",
    "method", r.Method,
    "path", r.URL.Path,
    "remote_addr", r.RemoteAddr)
// Output: {"time":"2024-01-01T12:00:00Z","level":"INFO","msg":"Request received","method":"GET","path":"/health","remote_addr":"127.0.0.1:54321"}
```

**Benefits:**
- **Machine-readable**: JSON format works with log aggregators (ELK, Datadog, etc.)
- **Structured fields**: Easy to query and filter in production
- **Context propagation**: Trace requests across services
- **Performance**: Efficient allocation and serialization
- **Level control**: DEBUG/INFO/WARN/ERROR levels

## Model Architecture

Both examples use a similar neural network architecture:

```
Input Layer (784 features)
    ↓
Dense Layer (128 neurons, ReLU activation)
    ↓
Dropout (0.2 rate)
    ↓
Dense Layer (64 neurons, ReLU activation)
    ↓
Dropout (0.2 rate)
    ↓
Output Layer (10 neurons, Softmax activation)
```

This architecture is:
- **Simple enough** to train quickly
- **Deep enough** to learn complex patterns
- **Regularized** with dropout to prevent overfitting
- **Suitable** for image classification tasks

## Performance Tips

1. **Subset Training**: The real MNIST example loads a subset (5000 samples) by default for faster demonstration. Change to 0 to use the full dataset:
   ```go
   data.LoadMNISTSubset("mnist_data", 0, 0)  // Full dataset
   ```

2. **Batch Size**: Larger batch sizes (32-128) train faster but may reduce accuracy

3. **Learning Rate**: The Adam optimizer uses 0.001 by default, which works well for MNIST

4. **Epochs**: 10 epochs is usually sufficient for MNIST to reach good accuracy

## Expected Results

### Synthetic Data (mnist_example)
- Accuracy: ~10% (random baseline for 10 classes)
- Purpose: Demonstrates the framework works correctly

### Real MNIST (mnist_real)
- Accuracy: 85-95% after 10 epochs
- Training time: 1-5 minutes depending on hardware
- Inference time: <2ms per image

## Extending the Examples

You can modify these examples to:
1. **Add more layers**: Increase model capacity
2. **Try different activations**: Experiment with Tanh, Sigmoid
3. **Adjust hyperparameters**: Learning rate, batch size, epochs
4. **Add regularization**: More dropout, batch normalization
5. **Implement data augmentation**: Rotation, scaling, noise
6. **Try different optimizers**: SGD with momentum, RMSprop

## Troubleshooting

### Download Issues
If MNIST download fails, the example falls back to synthetic data. Check:
- Internet connection
- Firewall settings
- Disk space in the cache directory

### Low Accuracy
- Ensure data is properly normalized
- Check for sufficient training epochs
- Verify model architecture is appropriate
- Consider adjusting learning rate

### Memory Issues
- Reduce batch size
- Use data subset for testing
- Ensure sufficient RAM for full dataset