# Image Processing - Go Implementation

This directory contains a Go implementation of image processing utilities. It provides a set of tools for manipulating and processing images efficiently.

## Features

- Image format conversion
- Image manipulation
- Efficient processing
- Concurrent operations
- Memory optimization

## Building

This project uses both Go modules and Bazel for building:

```bash
# Using Go
go build ./...

# Using Bazel
bazel build //domains/graphics/libs/imagine_go:...
```

## Testing

```bash
# Using Go
go test ./...

# Using Bazel
bazel test //domains/graphics/libs/imagine_go:...
```

## Example Usage

```go
// Example of image processing
processor := images.NewProcessor(
    images.WithConcurrency(4),
    images.WithMaxMemory(1024 * 1024 * 100), // 100MB
)

result, err := processor.ProcessImage(inputImage, images.Resize(800, 600))
if err != nil {
    log.Fatal(err)
}
```

```shell
bazel build //domains/graphics/libs/imagine_go
bazel-bin/domains/graphics/libs/imagine_go/images_/images domains/graphics/data/tippo.png 15
open domains/graphics/data/tippo.png domains/graphics/data/tippo.png.grey.Box.png domains/graphics/data/tippo.png.grey.X.png domains/graphics/data/tippo.png.grey.Y.png
```

![Box X](../../data/tippo.png.png)
![Box X](../../data/tippo.png.grey.X.png)
![Box Y](../../data/tippo.png.grey.Y.png)
![Box](../../data/tippo.png.grey.Box.png)
