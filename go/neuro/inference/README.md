# Neuro Inference Library

The Neuro Inference Library provides efficient model loading and inference capabilities for models trained with the Neuro deep learning framework. It supports model serialization, fast inference, batch processing, and various prediction modes.

## Important: Deployment Best Practices

⚠️ **Training vs Inference Deployments**

- **Training Applications**: Can embed training/test datasets for convenience during development and training
- **Inference/Production Applications**: Should ONLY load pre-trained model files, NOT raw training data
  - Models are typically 1-10 MB (much smaller than datasets)
  - Embedding datasets in production increases binary size unnecessarily
  - Training data is not needed for inference

**Good Practice:**
```go
// Production inference service
engine, _ := inference.LoadModelForInference("models/trained_model")
output, _ := engine.Predict(userInput)
```

**Bad Practice:**
```go
// DON'T embed training data in production
xTrain, yTrain, _, _ := data.LoadVendoredMNIST() // ❌ Unnecessary 11MB
```

## Features

- **Model Serialization**: Save and load trained models with weights and configurations
- **Fast Inference**: Optimized forward-only computation for production use
- **Batch Processing**: Efficient batch prediction support
- **Multiple Output Formats**: Get raw outputs, class predictions, or top-K results
- **Model Warmup**: Pre-compute optimizations for consistent latency
- **Checkpoint Management**: Save and restore training checkpoints

## Usage Patterns

### 1. Basic Inference

Load a trained model and make predictions:

```go
package main

import (
    "fmt"
    "log"
    
    "github.com/MoonBase/go/neuro/inference"
    "github.com/MoonBase/go/neuro/utils"
)

func main() {
    // Create inference engine
    engine := inference.NewInferenceEngine()
    
    // Load model from saved files
    err := engine.LoadModel("models/config.json", "models/weights.json")
    if err != nil {
        log.Fatal(err)
    }
    
    // Prepare input data (e.g., 28x28 image for MNIST)
    input := utils.NewTensor(1, 784)
    // ... fill input with preprocessed data ...
    
    // Make prediction
    output, err := engine.Predict(input)
    if err != nil {
        log.Fatal(err)
    }
    
    fmt.Printf("Prediction output: %v\n", output.Data)
}
```

### 2. Class Prediction

Get the predicted class and confidence:

```go
// Predict class index and confidence
classIdx, confidence, err := engine.PredictClass(input)
if err != nil {
    log.Fatal(err)
}
fmt.Printf("Predicted class: %d with confidence: %.2f%%\n", 
    classIdx, confidence*100)

// Or get class name if configured
className, confidence, err := engine.PredictClassName(input)
if err != nil {
    log.Fatal(err)
}
fmt.Printf("Predicted: %s (%.2f%% confidence)\n", 
    className, confidence*100)
```

### 3. Top-K Predictions

Get multiple predictions ranked by confidence:

```go
// Get top 5 predictions
indices, values, err := engine.PredictTopK(input, 5)
if err != nil {
    log.Fatal(err)
}

for i := 0; i < len(indices); i++ {
    fmt.Printf("%d. Class %d: %.2f%%\n", 
        i+1, indices[i], values[i]*100)
}
```

### 4. Batch Processing

Process multiple inputs efficiently:

```go
// Prepare batch of inputs
inputs := make([]*utils.Tensor, batchSize)
for i := 0; i < batchSize; i++ {
    inputs[i] = prepareInput(data[i])
}

// Batch prediction
outputs, err := engine.PredictBatch(inputs)
if err != nil {
    log.Fatal(err)
}

// Process results
for i, output := range outputs {
    processOutput(i, output)
}
```

### 5. Model Serialization

Save a trained model for inference:

```go
import (
    "github.com/MoonBase/go/neuro/inference"
    "github.com/MoonBase/go/neuro/network"
)

// After training your model...
func saveTrainedModel(model *network.Model) error {
    // Save model to directory
    err := inference.SaveModel(model, "models/my_model")
    if err != nil {
        return err
    }
    
    fmt.Println("Model saved successfully")
    return nil
}
```

### 6. Checkpoint Management

Save training checkpoints and resume from the best one:

```go
// Create checkpoint manager
checkpointMgr := inference.NewCheckpointManager("checkpoints", 5)

// During training loop
for epoch := 0; epoch < epochs; epoch++ {
    // ... training code ...
    
    // Save checkpoint after each epoch
    err := checkpointMgr.SaveCheckpoint(model, epoch, avgLoss)
    if err != nil {
        log.Printf("Failed to save checkpoint: %v", err)
    }
}

// Load latest checkpoint to resume training
model, err := checkpointMgr.LoadLatestCheckpoint()
if err != nil {
    log.Fatal("Failed to load checkpoint:", err)
}
```

### 7. Model Warmup

Prepare model for consistent inference latency:

```go
// Warmup with 10 dummy iterations
err := engine.Warmup(10)
if err != nil {
    log.Fatal("Warmup failed:", err)
}
fmt.Println("Model warmed up and ready for inference")
```

### 8. Model Configuration

The model configuration JSON format:

```json
{
  "version": "1.0",
  "model_type": "feedforward",
  "input_shape": [1, 784],
  "output_shape": [1, 10],
  "layers": [
    {
      "type": "Dense",
      "name": "Dense(784, 128, ReLU)",
      "params": {
        "input_size": 784,
        "output_size": 128,
        "activation": "ReLU"
      }
    },
    {
      "type": "Dropout",
      "name": "Dropout(0.20)",
      "params": {
        "rate": 0.2
      }
    },
    {
      "type": "Dense",
      "name": "Dense(128, 10, Softmax)",
      "params": {
        "input_size": 128,
        "output_size": 10,
        "activation": "Softmax"
      }
    }
  ],
  "classes": ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9"]
}
```

## Performance Optimization

### Memory Management

The inference engine reuses tensors where possible to minimize allocations:

```go
// Process stream of data with minimal allocations
for data := range dataStream {
    input := preprocessData(data)
    output, _ := engine.Predict(input)
    sendResult(output)
}
```

### Concurrent Inference

For handling multiple requests concurrently:

```go
func handleInferenceRequest(engine *inference.InferenceEngine, 
                           input *utils.Tensor, 
                           respChan chan<- *utils.Tensor) {
    output, err := engine.Predict(input)
    if err != nil {
        respChan <- nil
        return
    }
    respChan <- output
}

// Use goroutines for concurrent processing
for _, input := range inputs {
    go handleInferenceRequest(engine, input, respChan)
}
```

## Integration Examples

### Web Service

```go
func predictionHandler(engine *inference.InferenceEngine) http.HandlerFunc {
    return func(w http.ResponseWriter, r *http.Request) {
        // Parse input from request
        input, err := parseInput(r)
        if err != nil {
            http.Error(w, err.Error(), http.StatusBadRequest)
            return
        }
        
        // Make prediction
        output, err := engine.Predict(input)
        if err != nil {
            http.Error(w, err.Error(), http.StatusInternalServerError)
            return
        }
        
        // Return JSON response
        json.NewEncoder(w).Encode(map[string]interface{}{
            "prediction": output.Data,
        })
    }
}
```

### Stream Processing

```go
func processImageStream(engine *inference.InferenceEngine, 
                        images <-chan []byte, 
                        results chan<- int) {
    for imageData := range images {
        // Preprocess image
        input := preprocessImage(imageData)
        
        // Predict
        classIdx, _, err := engine.PredictClass(input)
        if err != nil {
            results <- -1
            continue
        }
        
        results <- classIdx
    }
}
```

## Error Handling

The inference library provides detailed error messages:

```go
output, err := engine.Predict(input)
if err != nil {
    switch {
    case strings.Contains(err.Error(), "invalid input shape"):
        // Handle shape mismatch
        log.Printf("Input shape error: %v", err)
    case strings.Contains(err.Error(), "model not loaded"):
        // Handle uninitialized model
        log.Fatal("Model must be loaded before inference")
    default:
        // Handle other errors
        log.Printf("Inference error: %v", err)
    }
}
```

## Best Practices

1. **Model Loading**: Load models once at startup and reuse the engine
2. **Input Validation**: Always validate input shapes before inference
3. **Batch Processing**: Use batch predictions for multiple inputs when possible
4. **Warmup**: Run warmup iterations for production services
5. **Error Handling**: Implement proper error handling for production systems
6. **Monitoring**: Track inference latency and throughput metrics

## Future Enhancements

- ONNX export/import support
- Model quantization for reduced memory usage
- GPU acceleration support
- Model ensemble inference
- Dynamic batching for variable-size inputs
- TensorRT integration for NVIDIA GPUs