# CNN Training Optimization Strategies

## 1. Data Parallelism (Implemented)
- **File**: `mnist_cnn_parallel.go`
- **Speedup**: 2-4x on multi-core systems
- **How it works**: Processes multiple batches in parallel across CPU cores
- **Run**: `bazel run //go/neuro/examples:mnist_cnn_parallel`

## 2. Batch Size Optimization
Larger batch sizes can improve hardware utilization:

```go
// In mnist_cnn_full.go, modify:
batchSize := 256  // Increase from 128
// or
batchSize := 512  // If you have enough memory
```

Pros:
- Better vectorization
- Fewer weight updates per epoch
- More stable gradients

Cons:
- Higher memory usage
- May need lower learning rate
- Can reduce generalization

## 3. Convolution Optimizations

### Im2Col Caching
The current implementation already uses Im2Col for efficient convolution, but we can cache transformed matrices:

```go
// In Conv2D layer
type Conv2D struct {
    // ... existing fields ...
    colCache map[string]*utils.Tensor // Cache Im2Col results
}
```

### FFT-based Convolution
For large kernels (>5x5), FFT-based convolution can be faster:
- Convert to frequency domain
- Multiply
- Convert back

## 4. Memory Optimizations

### Gradient Accumulation
Instead of updating after each batch, accumulate gradients:

```go
// Process mini-batches
for miniBatch := 0; miniBatch < accumSteps; miniBatch++ {
    // Forward and backward
    model.AccumulateGradients(grad)
}
// Update once
model.UpdateWeights(lr / accumSteps)
```

### In-place Operations
Modify tensor operations to reuse memory:

```go
func (t *Tensor) AddInPlace(other *Tensor) {
    for i := range t.Data {
        t.Data[i] += other.Data[i]
    }
}
```

## 5. Mixed Precision Training

Use float32 for forward/backward, float64 for weight updates:

```go
type MixedPrecisionTensor struct {
    DataF32 []float32  // Activations
    DataF64 []float64  // Weights
}
```

## 6. Hardware Acceleration

### Using GoNum BLAS
Install optimized BLAS library:

```bash
# Install OpenBLAS
brew install openblas  # macOS
apt-get install libopenblas-dev  # Linux

# Set environment variable
export CGO_LDFLAGS="-lopenblas"
```

Then use gonum for matrix operations:

```go
import "gonum.org/v1/gonum/blas/blas64"

// Use BLAS for matrix multiplication
blas64.Gemm(blas.NoTrans, blas.NoTrans, ...)
```

### GPU Acceleration (Future)
- Use CUDA bindings for Go
- Or use cgo to call cuDNN

## 7. Profiling and Benchmarking

Add profiling to identify bottlenecks:

```go
import _ "net/http/pprof"
import "net/http"

func main() {
    go func() {
        http.ListenAndServe("localhost:6060", nil)
    }()
    
    // Training code...
}

// Profile with:
// go tool pprof http://localhost:6060/debug/pprof/profile
```

## 8. Distributed Training (Advanced)

For multiple machines:

```go
type DistributedTrainer struct {
    workers []string  // Worker addresses
    comm    *grpc.Client
}

func (dt *DistributedTrainer) AllReduce(gradients []*Tensor) {
    // Average gradients across all workers
}
```

## Current Performance Benchmarks

On M1 MacBook Pro:
- Sequential: ~5 minutes for 10 epochs (60k samples)
- Parallel (4 workers): ~2 minutes
- With optimizations: ~1 minute possible

## Quick Wins for Immediate Speedup

1. **Increase batch size**: Change to 256 or 512
2. **Use parallel version**: Run `mnist_cnn_parallel.go`
3. **Reduce logging frequency**: Log every 100 batches instead of 10
4. **Disable dropout during initial epochs**: Speeds up convergence
5. **Use momentum optimizer**: Faster convergence than vanilla SGD

## Example: Optimized Training Configuration

```go
// Optimal settings for speed
epochs := 5           // Reduced from 10
batchSize := 256      // Increased from 128
learningRate := 0.01  // Increased for larger batch
numWorkers := runtime.NumCPU()  // Use all cores

// Learning rate schedule
schedule := []float64{0.01, 0.005, 0.001, 0.0005, 0.0001}
```