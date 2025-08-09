# Neuro - Deep Neural Network Library for Go

A pure Go implementation of a deep neural network library suitable for training classification models on images, text, and audio data.

## Project Structure

```
go/neuro/
├── network/          # Core neural network and training logic
│   ├── model.go      # Model class with forward/backward propagation
│   └── optimizer.go  # SGD, Adam, RMSprop optimizers
├── layers/           # Neural network layers
│   └── layers.go     # Dense, Dropout, BatchNorm layers
├── activations/      # Activation functions
│   └── activations.go # ReLU, Sigmoid, Tanh, Softmax
├── loss/            # Loss functions
│   └── loss.go      # MSE, CrossEntropy, CategoricalCrossEntropy
├── data/            # Data preprocessing and loading
│   └── dataset.go   # Dataset class, normalization, train/test split
├── utils/           # Utility functions
│   ├── tensor.go    # Tensor operations (add, multiply, matmul, etc.)
│   └── math.go      # Mathematical helper functions
└── BUILD.bazel      # Bazel build configuration files

```

## Features

### Core Components
- **Tensor Operations**: Efficient tensor math with broadcasting support
- **Automatic Differentiation**: Full backpropagation support
- **Layer Types**: Dense/Linear, Dropout, BatchNorm
- **Activations**: ReLU, Sigmoid, Tanh, Softmax
- **Loss Functions**: MSE, Binary CrossEntropy, Categorical CrossEntropy
- **Optimizers**: SGD (with momentum), Adam, RMSprop

### Data Processing
- Dataset batching with shuffle support
- Train/test splitting
- Normalization (standard and min-max scaling)
- One-hot encoding for categorical labels

## Usage Example

```go
package main

import (
    "github.com/MoonBase/go/neuro/network"
    "github.com/MoonBase/go/neuro/layers"
    "github.com/MoonBase/go/neuro/activations"
    "github.com/MoonBase/go/neuro/loss"
    "github.com/MoonBase/go/neuro/data"
    "github.com/MoonBase/go/neuro/utils"
)

func main() {
    // Create a model
    model := network.NewModel()
    
    // Add layers
    model.Add(layers.NewDense(784, 128, activations.NewReLU()))
    model.Add(layers.NewDropout(0.2))
    model.Add(layers.NewDense(128, 64, activations.NewReLU()))
    model.Add(layers.NewDense(64, 10, activations.NewSoftmax()))
    
    // Set loss and optimizer
    model.SetLoss(loss.NewCategoricalCrossEntropy())
    model.SetOptimizer(network.NewAdam(0.001))
    
    // Load and preprocess data
    xTrain, yTrain := loadData() // Your data loading function
    xTrain, _, _ = data.Normalize(xTrain)
    yTrain = data.OneHotEncode(labels, 10)
    
    // Create dataset
    dataset := data.NewDataset(xTrain, yTrain, 32, true)
    
    // Training loop
    epochs := 10
    for epoch := 0; epoch < epochs; epoch++ {
        dataset.Reset()
        totalLoss := 0.0
        batches := 0
        
        for dataset.HasNext() {
            xBatch, yBatch := dataset.NextBatch()
            loss := model.Train(xBatch, yBatch)
            totalLoss += loss
            batches++
        }
        
        avgLoss := totalLoss / float64(batches)
        fmt.Printf("Epoch %d: Loss = %.4f\n", epoch+1, avgLoss)
    }
    
    // Evaluation
    loss, accuracy := model.Evaluate(xTest, yTest)
    fmt.Printf("Test Loss: %.4f, Accuracy: %.2f%%\n", loss, accuracy*100)
}
```

## Building with Bazel

```bash
# Build the library
bazel build //go/neuro:neuro

# Run tests
bazel test //go/neuro/utils:utils_test
bazel test //go/neuro/network:network_test

# Build all targets
bazel build //go/neuro/...

# Test all targets
bazel test //go/neuro/...
```

## Generated Files and Directories

The following are **generated** and should NOT be committed to Git:

- `models/` - Trained model files (created by examples)
- `checkpoints/` - Training checkpoints
- `*.log` - Log files
- `bazel-*` - Bazel build artifacts

These are already in `.gitignore`.

## Inference Library

The framework now includes a comprehensive inference library for deploying trained models in production. See the [inference documentation](inference/README.md) for detailed usage.

### Key Features
- **Model Serialization**: Save and load trained models with configuration and weights
- **Fast Inference**: Optimized forward-only computation for production
- **Batch Processing**: Efficient batch prediction support
- **Multiple Output Formats**: Raw outputs, class predictions, or top-K results
- **Checkpoint Management**: Save and restore training checkpoints
- **Model Warmup**: Pre-compute optimizations for consistent latency

### Deployment Best Practices

⚠️ **Important Distinction:**
- **Training binaries**: Can embed datasets for convenience (e.g., MNIST data for training)
- **Production inference binaries**: Should ONLY include model weights, NOT training data
  - Trained models are small (typically 1-10 MB)
  - Datasets are large (MNIST is 11 MB, ImageNet is GBs)
  - Production services don't need training data

### Quick Example

```go
// Training and saving a model
model := buildModel()
trainModel(model, xTrain, yTrain)
inference.SaveModel(model, "models/my_model")

// Loading and using for inference
engine, _ := inference.LoadModelForInference("models/my_model")
output, _ := engine.Predict(input)
classIdx, confidence, _ := engine.PredictClass(input)
```

## Complete Example

See [examples/mnist_example.go](examples/mnist_example.go) for a complete end-to-end workflow demonstrating:
- Model building and configuration
- Training with checkpointing
- Model serialization
- Inference with various prediction modes
- Batch processing
- Performance optimization with warmup

To run the example:
```bash
bazel run //go/neuro/examples:mnist_example
```

## Next Steps for Development

### Immediate Priorities

1. **Convolutional Layers**
   - Implement Conv2D layer with padding and stride support
   - Add MaxPooling2D and AveragePooling2D layers
   - Implement Conv1D for sequence data

2. **Recurrent Layers**
   - Implement vanilla RNN layer
   - Add LSTM (Long Short-Term Memory) layer
   - Add GRU (Gated Recurrent Unit) layer
   - Implement bidirectional wrapper

3. **Advanced Features**
   - Learning rate scheduling (StepLR, ExponentialLR, CosineAnnealing)
   - Gradient clipping for stable training
   - Weight regularization (L1, L2)

4. **Data Pipeline**
   - Image augmentation (rotation, flip, crop, brightness)
   - Text tokenization and embedding layers
   - Audio preprocessing (MFCC, spectrogram)
   - Efficient data loading with goroutines

### Performance Optimizations

1. **Computation**
   - SIMD optimizations for tensor operations
   - GPU support via CUDA bindings
   - Parallel batch processing
   - Memory pooling for tensor allocation

2. **Model Export**
   - Complete ONNX export/import implementation
   - Model quantization for reduced memory usage
   - TensorRT integration for NVIDIA GPUs

### Testing & Documentation

1. **Testing**
   - Increase test coverage to >90%
   - Add integration tests
   - Benchmark performance tests
   - Gradient checking tests

2. **Documentation**
   - API documentation with examples
   - Tutorial notebooks
   - Performance benchmarks
   - Architecture diagrams

## Contributing

To contribute to this project:

1. Add new features in appropriate directories
2. Write comprehensive tests for new functionality
3. Update BUILD.bazel files for new source files
4. Follow Go best practices and conventions
5. Document public APIs with clear comments

## License

This project is part of the MoonBase repository.