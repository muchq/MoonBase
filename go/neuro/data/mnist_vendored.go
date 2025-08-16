package data

import (
	"compress/gzip"
	"embed"
	"encoding/binary"
	"fmt"
	"io"
	
	"github.com/muchq/moonbase/go/neuro/utils"
)

// Embed the MNIST dataset files directly into the binary
// This eliminates the need for downloads and external dependencies
//
//go:embed mnist_vendor/*.gz
var mnistFiles embed.FS

type VendoredMNISTLoader struct{}

func NewVendoredMNISTLoader() *VendoredMNISTLoader {
	return &VendoredMNISTLoader{}
}

func (m *VendoredMNISTLoader) Load() (*utils.Tensor, *utils.Tensor, *utils.Tensor, *utils.Tensor, error) {
	fmt.Println("Loading vendored MNIST dataset...")
	
	trainImages, err := m.loadEmbeddedImages("mnist_vendor/train-images-idx3-ubyte.gz")
	if err != nil {
		return nil, nil, nil, nil, fmt.Errorf("failed to load train images: %w", err)
	}
	
	trainLabels, err := m.loadEmbeddedLabels("mnist_vendor/train-labels-idx1-ubyte.gz")
	if err != nil {
		return nil, nil, nil, nil, fmt.Errorf("failed to load train labels: %w", err)
	}
	
	testImages, err := m.loadEmbeddedImages("mnist_vendor/t10k-images-idx3-ubyte.gz")
	if err != nil {
		return nil, nil, nil, nil, fmt.Errorf("failed to load test images: %w", err)
	}
	
	testLabels, err := m.loadEmbeddedLabels("mnist_vendor/t10k-labels-idx1-ubyte.gz")
	if err != nil {
		return nil, nil, nil, nil, fmt.Errorf("failed to load test labels: %w", err)
	}
	
	xTrain := m.preprocessImages(trainImages)
	yTrain := OneHotEncode(trainLabels, 10)
	xTest := m.preprocessImages(testImages)
	yTest := OneHotEncode(testLabels, 10)
	
	fmt.Printf("MNIST loaded: %d training samples, %d test samples\n", 
		xTrain.Shape[0], xTest.Shape[0])
	
	return xTrain, yTrain, xTest, yTest, nil
}

func (m *VendoredMNISTLoader) loadEmbeddedImages(path string) ([][]byte, error) {
	file, err := mnistFiles.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open embedded file %s: %w", path, err)
	}
	defer file.Close()
	
	gz, err := gzip.NewReader(file)
	if err != nil {
		return nil, fmt.Errorf("failed to create gzip reader: %w", err)
	}
	defer gz.Close()
	
	var magic, numImages, numRows, numCols int32
	binary.Read(gz, binary.BigEndian, &magic)
	binary.Read(gz, binary.BigEndian, &numImages)
	binary.Read(gz, binary.BigEndian, &numRows)
	binary.Read(gz, binary.BigEndian, &numCols)
	
	if magic != 2051 {
		return nil, fmt.Errorf("invalid magic number for images: %d", magic)
	}
	
	imageSize := int(numRows * numCols)
	images := make([][]byte, numImages)
	
	for i := 0; i < int(numImages); i++ {
		images[i] = make([]byte, imageSize)
		if _, err := io.ReadFull(gz, images[i]); err != nil {
			return nil, fmt.Errorf("failed to read image %d: %w", i, err)
		}
	}
	
	return images, nil
}

func (m *VendoredMNISTLoader) loadEmbeddedLabels(path string) ([]int, error) {
	file, err := mnistFiles.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open embedded file %s: %w", path, err)
	}
	defer file.Close()
	
	gz, err := gzip.NewReader(file)
	if err != nil {
		return nil, fmt.Errorf("failed to create gzip reader: %w", err)
	}
	defer gz.Close()
	
	var magic, numLabels int32
	binary.Read(gz, binary.BigEndian, &magic)
	binary.Read(gz, binary.BigEndian, &numLabels)
	
	if magic != 2049 {
		return nil, fmt.Errorf("invalid magic number for labels: %d", magic)
	}
	
	labels := make([]int, numLabels)
	labelBytes := make([]byte, numLabels)
	
	if _, err := io.ReadFull(gz, labelBytes); err != nil {
		return nil, fmt.Errorf("failed to read labels: %w", err)
	}
	
	for i, b := range labelBytes {
		labels[i] = int(b)
	}
	
	return labels, nil
}

func (m *VendoredMNISTLoader) preprocessImages(images [][]byte) *utils.Tensor {
	numImages := len(images)
	imageSize := len(images[0])
	
	tensor := utils.NewTensor(numImages, imageSize)
	
	for i, img := range images {
		for j, pixel := range img {
			tensor.Set(float64(pixel)/255.0, i, j)
		}
	}
	
	// Normalize the data
	tensor, _, _ = Normalize(tensor)
	
	return tensor
}

// LoadVendoredMNIST loads the embedded MNIST dataset
func LoadVendoredMNIST() (*utils.Tensor, *utils.Tensor, *utils.Tensor, *utils.Tensor, error) {
	loader := NewVendoredMNISTLoader()
	return loader.Load()
}

// LoadVendoredMNISTSubset loads a subset of the embedded MNIST dataset
func LoadVendoredMNISTSubset(trainSize, testSize int) (*utils.Tensor, *utils.Tensor, *utils.Tensor, *utils.Tensor, error) {
	xTrain, yTrain, xTest, yTest, err := LoadVendoredMNIST()
	if err != nil {
		return nil, nil, nil, nil, err
	}
	
	// Use subset if specified
	if trainSize > 0 && trainSize < xTrain.Shape[0] {
		trainIndices := make([]int, trainSize)
		for i := range trainIndices {
			trainIndices[i] = i
		}
		xTrain = extractSamples(xTrain, trainIndices)
		yTrain = extractSamples(yTrain, trainIndices)
	}
	
	if testSize > 0 && testSize < xTest.Shape[0] {
		testIndices := make([]int, testSize)
		for i := range testIndices {
			testIndices[i] = i
		}
		xTest = extractSamples(xTest, testIndices)
		yTest = extractSamples(yTest, testIndices)
	}
	
	return xTrain, yTrain, xTest, yTest, nil
}

// MNISTInfo provides information about the MNIST dataset
type MNISTInfo struct {
	TrainingSamples int
	TestSamples     int
	ImageWidth      int
	ImageHeight     int
	NumClasses      int
	Classes         []string
}

// GetMNISTInfo returns information about the MNIST dataset
func GetMNISTInfo() MNISTInfo {
	return MNISTInfo{
		TrainingSamples: 60000,
		TestSamples:     10000,
		ImageWidth:      28,
		ImageHeight:     28,
		NumClasses:      10,
		Classes:         []string{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"},
	}
}