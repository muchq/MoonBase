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
bazel build //go/images:...
```

## Testing

```bash
# Using Go
go test ./...

# Using Bazel
bazel test //go/images:...
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
bazel build //go/images
bazel-bin/go/images/images_/images static_content/tippo.png 15
open static_content/tippo.png static_content/tippo.png.grey.Box.png static_content/tippo.png.grey.X.png static_content/tippo.png.grey.Y.png
```

![Box X](../../static_content/tippo.png.png)
![Box X](../../static_content/tippo.png.grey.X.png)
![Box Y](../../static_content/tippo.png.grey.Y.png)
![Box](../../static_content/tippo.png.grey.Box.png)
