# MNIST Dataset Vendoring

This directory contains the vendored MNIST dataset files. The MNIST dataset is small enough (~11 MB compressed) to be included directly in the repository, eliminating the need for runtime downloads.

## Dataset Files

The MNIST dataset consists of four gzip-compressed files:
- `train-images-idx3-ubyte.gz` - 60,000 training images (9.4 MB)
- `train-labels-idx1-ubyte.gz` - 60,000 training labels (28 KB)
- `t10k-images-idx3-ubyte.gz` - 10,000 test images (1.6 MB)
- `t10k-labels-idx1-ubyte.gz` - 10,000 test labels (4.4 KB)

**Total size: ~11 MB compressed**

## Files Already Included

✅ **The MNIST dataset files are already included in this repository.**  
No download or setup is required - the data is embedded directly into the Go binary at compile time.

## Using Vendored Data

The framework provides two ways to load MNIST:

### Option 1: Embedded Data (Recommended)
```go
import "github.com/MoonBase/go/neuro/data"

// Load full dataset from embedded files
xTrain, yTrain, xTest, yTest, err := data.LoadVendoredMNIST()

// Or load a subset for faster testing
xTrain, yTrain, xTest, yTest, err := data.LoadVendoredMNISTSubset(5000, 1000)
```

### Option 2: Download on Demand
```go
// Downloads from web if not cached
xTrain, yTrain, xTest, yTest, err := data.LoadMNIST("cache_dir")
```

## Advantages of Vendoring

1. **No Internet Required**: Works offline after initial setup
2. **Faster Loading**: No download wait time
3. **Reproducible**: Same data for all users
4. **CI/CD Friendly**: No external dependencies
5. **Version Control**: Data versioned with code

## File Format

MNIST uses the IDX file format:
- Magic number for validation
- Dimensions (number of images, height, width)
- Raw pixel data (0-255 grayscale values)

The Go code handles:
- Gzip decompression
- Binary parsing
- Normalization (0-1 range)
- Tensor conversion

## License

The MNIST dataset is made available by Yann LeCun and Corinna Cortes under the Creative Commons Attribution-Share Alike 3.0 license. See http://yann.lecun.com/exdb/mnist/ for details.