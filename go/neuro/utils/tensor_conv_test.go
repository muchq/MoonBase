package utils

import (
	"fmt"
	"math"
	"testing"
)

func TestCalculateConvOutputSize(t *testing.T) {
	tests := []struct {
		name       string
		inputSize  int
		kernelSize int
		stride     int
		padding    int
		expected   int
	}{
		{"No padding, stride 1", 5, 3, 1, 0, 3},
		{"With padding, stride 1", 5, 3, 1, 1, 5},
		{"No padding, stride 2", 5, 3, 2, 0, 2},
		{"Large kernel", 10, 5, 1, 0, 6},
		{"Same padding effect", 28, 5, 1, 2, 28},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			result := CalculateConvOutputSize(tc.inputSize, tc.kernelSize, tc.stride, tc.padding)
			if result != tc.expected {
				t.Errorf("Expected %d, got %d", tc.expected, result)
			}
		})
	}
}

func TestCalculateSamePaddingAsymmetric(t *testing.T) {
	tests := []struct {
		name         string
		inputSize    int
		kernelSize   int
		stride       int
		expectedLeft int
		expectedRight int
	}{
		{"3x3 kernel, stride 1", 28, 3, 1, 1, 1},
		{"5x5 kernel, stride 1", 28, 5, 1, 2, 2},
		{"7x7 kernel, stride 1", 28, 7, 1, 3, 3},
		{"3x3 kernel, stride 2", 28, 3, 2, 0, 1},
		{"Even kernel 4x4, stride 1", 28, 4, 1, 1, 2},
		{"Even kernel 2x2, stride 1", 28, 2, 1, 0, 1},
		{"5x5 kernel, stride 2", 28, 5, 2, 1, 2},
		{"Input 32, 3x3 kernel, stride 1", 32, 3, 1, 1, 1},
		{"Input 32, 4x4 kernel, stride 2", 32, 4, 2, 1, 1},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			padLeft, padRight := CalculateSamePaddingAsymmetric(tc.inputSize, tc.kernelSize, tc.stride)
			if padLeft != tc.expectedLeft || padRight != tc.expectedRight {
				t.Errorf("Expected padding (%d, %d), got (%d, %d)", tc.expectedLeft, tc.expectedRight, padLeft, padRight)
			}
			
			// Verify that padding achieves "same" output size when stride=1
			if tc.stride == 1 {
				outputSize := CalculateConvOutputSize(tc.inputSize, tc.kernelSize, tc.stride, padLeft)
				// For asymmetric padding, we need to account for both sides
				outputSize = (tc.inputSize + padLeft + padRight - tc.kernelSize) / tc.stride + 1
				if outputSize != tc.inputSize {
					t.Errorf("With stride=1, expected output size %d to match input size, got %d", tc.inputSize, outputSize)
				}
			}
		})
	}
}

func TestPad2D(t *testing.T) {
	// Create a simple 4D tensor (1, 1, 3, 3)
	data := []float64{
		1, 2, 3,
		4, 5, 6,
		7, 8, 9,
	}
	input := NewTensorFromData(data, 1, 1, 3, 3)

	// Pad with 1 on each side
	padded := input.Pad2D(1, 1)

	// Check shape
	expectedShape := []int{1, 1, 5, 5}
	for i, dim := range expectedShape {
		if padded.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, padded.Shape[i])
		}
	}

	// Check that original data is in the center
	expected := []float64{
		0, 0, 0, 0, 0,
		0, 1, 2, 3, 0,
		0, 4, 5, 6, 0,
		0, 7, 8, 9, 0,
		0, 0, 0, 0, 0,
	}

	for i, v := range expected {
		if math.Abs(padded.Data[i]-v) > 1e-9 {
			t.Errorf("Data[%d]: expected %f, got %f", i, v, padded.Data[i])
		}
	}
}

func TestIm2Col(t *testing.T) {
	// Create a simple 4D tensor (1, 1, 4, 4)
	data := []float64{
		1, 2, 3, 4,
		5, 6, 7, 8,
		9, 10, 11, 12,
		13, 14, 15, 16,
	}
	input := NewTensorFromData(data, 1, 1, 4, 4)

	// Apply Im2Col with 2x2 kernel, stride 1, no padding
	col := input.Im2Col(2, 2, 1, 0, 0)

	// Expected shape: (9, 4) - 9 patches, 4 values per patch
	if col.Shape[0] != 9 || col.Shape[1] != 4 {
		t.Errorf("Expected shape (9, 4), got (%d, %d)", col.Shape[0], col.Shape[1])
	}

	// Check first patch (top-left 2x2)
	expectedFirstPatch := []float64{1, 2, 5, 6}
	for i, v := range expectedFirstPatch {
		if math.Abs(col.Data[i]-v) > 1e-9 {
			t.Errorf("First patch[%d]: expected %f, got %f", i, v, col.Data[i])
		}
	}
}

func TestCol2Im(t *testing.T) {
	// Create a column matrix representing 2x2 patches
	// 4 patches from a 3x3 image with 2x2 kernel, stride 1
	colData := []float64{
		1, 1, 1, 1, // First patch
		2, 2, 2, 2, // Second patch
		3, 3, 3, 3, // Third patch
		4, 4, 4, 4, // Fourth patch
	}
	col := NewTensorFromData(colData, 4, 4)

	// Convert back to image
	img := Col2Im(col, 1, 1, 3, 3, 2, 2, 1, 0, 0)

	// Check shape
	expectedShape := []int{1, 1, 3, 3}
	for i, dim := range expectedShape {
		if img.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, img.Shape[i])
		}
	}

	// The overlapping regions should sum
	// Expected (approximately):
	// 1  3  2
	// 4  10 6
	// 3  7  4
	expected := []float64{1, 3, 2, 4, 10, 6, 3, 7, 4}
	for i, v := range expected {
		if math.Abs(img.Data[i]-v) > 1e-9 {
			t.Errorf("Data[%d]: expected %f, got %f", i, v, img.Data[i])
		}
	}
}

func TestGet4DSet4D(t *testing.T) {
	// Create a 4D tensor
	tensor := NewTensor(2, 3, 4, 5)

	// Set some values
	tensor.Set4D(0, 0, 0, 0, 1.0)
	tensor.Set4D(1, 2, 3, 4, 2.0)
	tensor.Set4D(0, 1, 2, 3, 3.0)

	// Get and verify
	if v := tensor.Get4D(0, 0, 0, 0); math.Abs(v-1.0) > 1e-9 {
		t.Errorf("Expected 1.0, got %f", v)
	}
	if v := tensor.Get4D(1, 2, 3, 4); math.Abs(v-2.0) > 1e-9 {
		t.Errorf("Expected 2.0, got %f", v)
	}
	if v := tensor.Get4D(0, 1, 2, 3); math.Abs(v-3.0) > 1e-9 {
		t.Errorf("Expected 3.0, got %f", v)
	}
}

func TestMaxPool2DIndices(t *testing.T) {
	// Create a simple 4D tensor (1, 1, 4, 4)
	data := []float64{
		1, 2, 3, 4,
		5, 6, 7, 8,
		9, 10, 11, 12,
		13, 14, 15, 16,
	}
	input := NewTensorFromData(data, 1, 1, 4, 4)

	// Apply 2x2 max pooling with stride 2
	output, indices := input.MaxPool2DIndices(2, 2, 2)

	// Check output shape (1, 1, 2, 2)
	expectedShape := []int{1, 1, 2, 2}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}

	// Check max values
	expectedMax := []float64{6, 8, 14, 16}
	for i, v := range expectedMax {
		if math.Abs(output.Data[i]-v) > 1e-9 {
			t.Errorf("Max[%d]: expected %f, got %f", i, v, output.Data[i])
		}
	}

	// Check indices point to correct max positions
	expectedIndices := []int{5, 7, 13, 15}
	for i, idx := range expectedIndices {
		if indices[i] != idx {
			t.Errorf("Index[%d]: expected %d, got %d", i, idx, indices[i])
		}
	}
}

func TestTransposeAxes(t *testing.T) {
	// Create a 4D tensor with known values
	data := make([]float64, 24)
	for i := range data {
		data[i] = float64(i)
	}
	tensor := NewTensorFromData(data, 2, 3, 2, 2)

	tests := []struct {
		name     string
		axes     []int
		expected []int // Expected shape after transpose
	}{
		{
			name:     "Identity transpose",
			axes:     []int{0, 1, 2, 3},
			expected: []int{2, 3, 2, 2},
		},
		{
			name:     "Swap batch and channels",
			axes:     []int{1, 0, 2, 3},
			expected: []int{3, 2, 2, 2},
		},
		{
			name:     "Move channels to end",
			axes:     []int{0, 2, 3, 1},
			expected: []int{2, 2, 2, 3},
		},
		{
			name:     "Reverse all dimensions",
			axes:     []int{3, 2, 1, 0},
			expected: []int{2, 2, 3, 2},
		},
		{
			name:     "Complex permutation",
			axes:     []int{2, 0, 3, 1},
			expected: []int{2, 2, 2, 3},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			result := tensor.TransposeAxes(tc.axes[0], tc.axes[1], tc.axes[2], tc.axes[3])
			
			// Check shape
			for i, dim := range tc.expected {
				if result.Shape[i] != dim {
					t.Errorf("Shape[%d]: expected %d, got %d", i, dim, result.Shape[i])
				}
			}
			
			// Verify data integrity - total sum should be preserved
			originalSum := 0.0
			for _, v := range tensor.Data {
				originalSum += v
			}
			resultSum := 0.0
			for _, v := range result.Data {
				resultSum += v
			}
			if math.Abs(originalSum-resultSum) > 1e-9 {
				t.Errorf("Data sum changed: original=%f, result=%f", originalSum, resultSum)
			}
		})
	}
}

func TestTransposeAxesSpecificValues(t *testing.T) {
	// Test with specific values to verify correct transposition
	// Create 2x2x2x2 tensor with unique values
	data := []float64{
		// Batch 0
		// Channel 0
		1, 2,  // H=0
		3, 4,  // H=1
		// Channel 1  
		5, 6,  // H=0
		7, 8,  // H=1
		
		// Batch 1
		// Channel 0
		9, 10,   // H=0
		11, 12,  // H=1
		// Channel 1
		13, 14,  // H=0
		15, 16,  // H=1
	}
	tensor := NewTensorFromData(data, 2, 2, 2, 2)
	
	// Transpose to (batch, height, width, channels) - common for some operations
	result := tensor.TransposeAxes(0, 2, 3, 1)
	
	// Expected shape: (2, 2, 2, 2)
	expectedShape := []int{2, 2, 2, 2}
	for i, dim := range expectedShape {
		if result.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, result.Shape[i])
		}
	}
	
	// Verify specific values
	// Original tensor[0,0,0,0] = 1 should be at result[0,0,0,0]
	if result.Get4D(0, 0, 0, 0) != 1 {
		t.Errorf("Expected 1 at [0,0,0,0], got %f", result.Get4D(0, 0, 0, 0))
	}
	
	// Original tensor[0,1,0,0] = 5 should be at result[0,0,0,1]
	if result.Get4D(0, 0, 0, 1) != 5 {
		t.Errorf("Expected 5 at [0,0,0,1], got %f", result.Get4D(0, 0, 0, 1))
	}
}

func TestTransposeAxesInvalidInput(t *testing.T) {
	// Test that non-4D tensor causes panic
	tensor3D := NewTensor(2, 3, 4)
	
	defer func() {
		if r := recover(); r == nil {
			t.Error("Expected panic for non-4D tensor")
		}
	}()
	
	tensor3D.TransposeAxes(0, 1, 2, 3)
}

func TestTransposeAxesChainedOperations(t *testing.T) {
	// Test that transpose operations can be chained correctly
	tensor := NewTensor(2, 3, 4, 5)
	for i := range tensor.Data {
		tensor.Data[i] = float64(i)
	}
	
	// Transpose and then transpose back
	transposed := tensor.TransposeAxes(1, 0, 2, 3)
	restored := transposed.TransposeAxes(1, 0, 2, 3)
	
	// Should have original shape
	for i, dim := range tensor.Shape {
		if restored.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, restored.Shape[i])
		}
	}
	
	// Data should match original
	for i := range tensor.Data {
		if math.Abs(restored.Data[i]-tensor.Data[i]) > 1e-9 {
			t.Errorf("Data[%d]: expected %f, got %f", i, tensor.Data[i], restored.Data[i])
		}
	}
}

func TestTransposeAxesWithConvOutput(t *testing.T) {
	// Test transpose commonly used after convolution
	// Conv output is typically (batch, out_channels, height, width)
	// Sometimes we need (batch, height, width, out_channels)
	
	convOutput := NewTensor(32, 64, 28, 28)
	for i := range convOutput.Data {
		convOutput.Data[i] = float64(i % 100) * 0.01
	}
	
	// Transpose to channels-last format
	transposed := convOutput.TransposeAxes(0, 2, 3, 1)
	
	// Check shape
	if transposed.Shape[0] != 32 || transposed.Shape[1] != 28 || 
	   transposed.Shape[2] != 28 || transposed.Shape[3] != 64 {
		t.Errorf("Expected shape (32, 28, 28, 64), got %v", transposed.Shape)
	}
}

func BenchmarkTransposeAxes(b *testing.B) {
	sizes := [][]int{
		{2, 3, 32, 32},    // Small
		{8, 16, 64, 64},   // Medium
		{32, 64, 128, 128}, // Large
	}
	
	for _, shape := range sizes {
		name := fmt.Sprintf("shape_%dx%dx%dx%d", shape[0], shape[1], shape[2], shape[3])
		b.Run(name, func(b *testing.B) {
			tensor := NewTensor(shape...)
			for i := range tensor.Data {
				tensor.Data[i] = float64(i) * 0.01
			}
			
			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				_ = tensor.TransposeAxes(0, 2, 3, 1)
			}
		})
	}
}

func BenchmarkIm2Col(b *testing.B) {
	sizes := []struct {
		batch, channels, height, width int
		kernelH, kernelW, stride       int
	}{
		{1, 3, 28, 28, 3, 3, 1},    // Small
		{32, 3, 32, 32, 5, 5, 1},   // Medium
		{64, 64, 14, 14, 3, 3, 1},  // After pooling
		{128, 128, 7, 7, 3, 3, 1},  // Deep layer
	}

	for _, s := range sizes {
		name := fmt.Sprintf("b%d_c%d_h%d_w%d_k%d_s%d",
			s.batch, s.channels, s.height, s.width, s.kernelH, s.stride)
		b.Run(name, func(b *testing.B) {
			size := s.batch * s.channels * s.height * s.width
			data := make([]float64, size)
			for i := range data {
				data[i] = float64(i) * 0.01
			}
			tensor := NewTensorFromData(data, s.batch, s.channels, s.height, s.width)

			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				_ = tensor.Im2Col(s.kernelH, s.kernelW, s.stride, 0, 0)
			}
		})
	}
}