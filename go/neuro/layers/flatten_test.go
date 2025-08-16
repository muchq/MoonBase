package layers

import (
	"fmt"
	"math"
	"testing"

	"github.com/muchq/moonbase/go/neuro/utils"
)

func TestFlattenForward(t *testing.T) {
	tests := []struct {
		name        string
		inputShape  []int
		outputShape []int
	}{
		{
			name:        "3D input (batch, height, width)",
			inputShape:  []int{2, 3, 4},
			outputShape: []int{2, 12},
		},
		{
			name:        "4D input (batch, channels, height, width)",
			inputShape:  []int{2, 3, 4, 5},
			outputShape: []int{2, 60},
		},
		{
			name:        "2D input (already flat)",
			inputShape:  []int{5, 10},
			outputShape: []int{5, 10},
		},
		{
			name:        "5D input",
			inputShape:  []int{2, 2, 3, 4, 5},
			outputShape: []int{2, 120},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			// Create input tensor with sequential data
			size := 1
			for _, dim := range tc.inputShape {
				size *= dim
			}
			data := make([]float64, size)
			for i := range data {
				data[i] = float64(i)
			}
			input := utils.NewTensorFromData(data, tc.inputShape...)

			// Create and apply flatten layer
			flatten := NewFlatten()
			output := flatten.Forward(input, false)

			// Check output shape
			if len(output.Shape) != len(tc.outputShape) {
				t.Errorf("Expected output shape dimension %d, got %d", len(tc.outputShape), len(output.Shape))
			}
			for i := range tc.outputShape {
				if output.Shape[i] != tc.outputShape[i] {
					t.Errorf("Expected shape[%d]=%d, got %d", i, tc.outputShape[i], output.Shape[i])
				}
			}

			// Check data preservation
			for i := range data {
				if math.Abs(output.Data[i]-data[i]) > 1e-9 {
					t.Errorf("Data at position %d: expected %f, got %f", i, data[i], output.Data[i])
				}
			}
		})
	}
}

func TestFlattenBackward(t *testing.T) {
	tests := []struct {
		name       string
		inputShape []int
	}{
		{
			name:       "3D input",
			inputShape: []int{2, 3, 4},
		},
		{
			name:       "4D input",
			inputShape: []int{2, 3, 4, 5},
		},
		{
			name:       "5D input",
			inputShape: []int{1, 2, 3, 4, 5},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			// Create input tensor
			size := 1
			for _, dim := range tc.inputShape {
				size *= dim
			}
			data := make([]float64, size)
			for i := range data {
				data[i] = float64(i)
			}
			input := utils.NewTensorFromData(data, tc.inputShape...)

			// Forward pass
			flatten := NewFlatten()
			output := flatten.Forward(input, true)

			// Create gradient tensor (same shape as output)
			gradData := make([]float64, len(output.Data))
			for i := range gradData {
				gradData[i] = float64(i) * 0.1
			}
			gradOutput := utils.NewTensorFromData(gradData, output.Shape...)

			// Backward pass
			gradInput := flatten.Backward(gradOutput)

			// Check gradient shape matches original input shape
			if len(gradInput.Shape) != len(tc.inputShape) {
				t.Errorf("Expected gradient shape dimension %d, got %d", len(tc.inputShape), len(gradInput.Shape))
			}
			for i := range tc.inputShape {
				if gradInput.Shape[i] != tc.inputShape[i] {
					t.Errorf("Gradient shape[%d]: expected %d, got %d", i, tc.inputShape[i], gradInput.Shape[i])
				}
			}

			// Check gradient data preservation
			for i := range gradData {
				if math.Abs(gradInput.Data[i]-gradData[i]) > 1e-9 {
					t.Errorf("Gradient at position %d: expected %f, got %f", i, gradData[i], gradInput.Data[i])
				}
			}
		})
	}
}

func TestFlattenNoParams(t *testing.T) {
	flatten := NewFlatten()

	// Test GetParams returns empty
	params := flatten.GetParams()
	if len(params) != 0 {
		t.Errorf("Expected 0 parameters, got %d", len(params))
	}

	// Test SetParams with empty slice works
	flatten.SetParams([]*utils.Tensor{})

	// Test SetParams with non-empty slice panics
	defer func() {
		if r := recover(); r == nil {
			t.Error("Expected panic when setting parameters on Flatten layer")
		}
	}()
	flatten.SetParams([]*utils.Tensor{utils.NewTensor(1, 1)})
}

func TestFlattenUpdateWeights(t *testing.T) {
	flatten := NewFlatten()
	// Should do nothing and not panic
	flatten.UpdateWeights(0.01)
}

func TestFlattenName(t *testing.T) {
	flatten := NewFlatten()
	if flatten.Name() != "Flatten" {
		t.Errorf("Expected name 'Flatten', got '%s'", flatten.Name())
	}
}

func TestFlattenBackwardBeforeForward(t *testing.T) {
	flatten := NewFlatten()
	gradOutput := utils.NewTensor(2, 10)

	defer func() {
		if r := recover(); r == nil {
			t.Error("Expected panic when calling Backward before Forward")
		}
	}()
	flatten.Backward(gradOutput)
}

func TestFlattenSingleElement(t *testing.T) {
	// Test with single element tensor
	input := utils.NewTensorFromData([]float64{42.0}, 1, 1, 1, 1)
	
	flatten := NewFlatten()
	output := flatten.Forward(input, false)
	
	// Should produce (1, 1) shape
	if output.Shape[0] != 1 || output.Shape[1] != 1 {
		t.Errorf("Expected shape (1, 1), got %v", output.Shape)
	}
	if math.Abs(output.Data[0]-42.0) > 1e-9 {
		t.Errorf("Expected 42.0, got %f", output.Data[0])
	}
}

func TestFlattenHighDimensional(t *testing.T) {
	// Test with 6D tensor
	shape := []int{2, 3, 4, 5, 2, 3}
	totalElements := 1
	for _, dim := range shape {
		totalElements *= dim
	}
	
	data := make([]float64, totalElements)
	for i := range data {
		data[i] = float64(i) * 0.1
	}
	input := utils.NewTensorFromData(data, shape...)
	
	flatten := NewFlatten()
	output := flatten.Forward(input, true)
	
	// Check output shape
	expectedFeatures := totalElements / shape[0]
	if output.Shape[0] != shape[0] || output.Shape[1] != expectedFeatures {
		t.Errorf("Expected shape (%d, %d), got %v", shape[0], expectedFeatures, output.Shape)
	}
	
	// Test backward pass
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = float64(i) * 0.01
	}
	
	gradInput := flatten.Backward(gradOutput)
	
	// Check gradient shape matches original
	if len(gradInput.Shape) != len(shape) {
		t.Errorf("Expected %d dimensions, got %d", len(shape), len(gradInput.Shape))
	}
	for i, dim := range shape {
		if gradInput.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, gradInput.Shape[i])
		}
	}
}

func TestFlattenBatchSizeOne(t *testing.T) {
	// Test with batch size of 1
	input := utils.NewTensor(1, 10, 20, 30)
	for i := range input.Data {
		input.Data[i] = float64(i) * 0.001
	}
	
	flatten := NewFlatten()
	output := flatten.Forward(input, false)
	
	if output.Shape[0] != 1 || output.Shape[1] != 6000 {
		t.Errorf("Expected shape (1, 6000), got %v", output.Shape)
	}
}

func TestFlattenLargeBatch(t *testing.T) {
	// Test with large batch size
	batchSize := 128
	input := utils.NewTensor(batchSize, 3, 32, 32)
	
	flatten := NewFlatten()
	output := flatten.Forward(input, false)
	
	expectedFeatures := 3 * 32 * 32
	if output.Shape[0] != batchSize || output.Shape[1] != expectedFeatures {
		t.Errorf("Expected shape (%d, %d), got %v", batchSize, expectedFeatures, output.Shape)
	}
}

func TestFlattenDataIntegrity(t *testing.T) {
	// Test that flattening preserves data order correctly
	input := utils.NewTensor(2, 2, 3, 3)
	
	// Fill with sequential values
	for i := range input.Data {
		input.Data[i] = float64(i)
	}
	
	flatten := NewFlatten()
	output := flatten.Forward(input, false)
	
	// Data should be unchanged, just reshaped
	for i := range input.Data {
		if math.Abs(output.Data[i]-input.Data[i]) > 1e-9 {
			t.Errorf("Data at position %d changed: expected %f, got %f", 
				i, input.Data[i], output.Data[i])
		}
	}
	
	// Test backward preserves gradient order
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = float64(i) * 0.1
	}
	
	gradInput := flatten.Backward(gradOutput)
	
	for i := range gradOutput.Data {
		if math.Abs(gradInput.Data[i]-gradOutput.Data[i]) > 1e-9 {
			t.Errorf("Gradient at position %d changed: expected %f, got %f",
				i, gradOutput.Data[i], gradInput.Data[i])
		}
	}
}

func TestFlattenMemoryAliasing(t *testing.T) {
	// Test that flatten creates proper views without unintended aliasing
	input := utils.NewTensor(2, 3, 4, 5)
	for i := range input.Data {
		input.Data[i] = float64(i)
	}
	
	flatten := NewFlatten()
	output1 := flatten.Forward(input, false)
	
	// Modify input
	input.Data[0] = 999.0
	
	// Output should share the data (in this implementation)
	// This tests the current behavior - adjust if implementation changes
	if math.Abs(output1.Data[0]-999.0) > 1e-9 {
		// If this fails, it means the implementation creates a copy
		// which is also valid, just different
		t.Logf("Note: Flatten creates a copy rather than a view")
	}
}

func TestFlattenConsecutiveCalls(t *testing.T) {
	// Test multiple consecutive flatten operations
	flatten := NewFlatten()
	
	// First call
	input1 := utils.NewTensor(2, 3, 4)
	_ = flatten.Forward(input1, true)
	
	// Second call with different shape
	input2 := utils.NewTensor(3, 2, 5, 6)
	output2 := flatten.Forward(input2, true)
	
	// Backward for second
	gradOutput2 := utils.NewTensor(output2.Shape...)
	gradInput2 := flatten.Backward(gradOutput2)
	
	// Check that backward uses the most recent forward's shape
	if len(gradInput2.Shape) != len(input2.Shape) {
		t.Error("Backward should use the most recent forward's shape")
	}
	for i, dim := range input2.Shape {
		if gradInput2.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, gradInput2.Shape[i])
		}
	}
}

func TestFlattenEdgeCaseShapes(t *testing.T) {
	tests := []struct {
		name  string
		shape []int
	}{
		{"Very flat", []int{1000, 1}},
		{"Very tall", []int{1, 1000}},
		{"Single batch many features", []int{1, 1, 1, 1000}},
		{"Many batches single feature", []int{1000, 1, 1, 1}},
		{"Powers of 2", []int{8, 16, 32, 64}},
		{"Prime numbers", []int{7, 11, 13, 17}},
	}
	
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			totalElements := 1
			for _, dim := range tc.shape {
				totalElements *= dim
			}
			
			data := make([]float64, totalElements)
			input := utils.NewTensorFromData(data, tc.shape...)
			
			flatten := NewFlatten()
			output := flatten.Forward(input, false)
			
			expectedFeatures := totalElements / tc.shape[0]
			if output.Shape[0] != tc.shape[0] || output.Shape[1] != expectedFeatures {
				t.Errorf("Expected shape (%d, %d), got %v", 
					tc.shape[0], expectedFeatures, output.Shape)
			}
		})
	}
}

func BenchmarkFlatten(b *testing.B) {
	sizes := [][]int{
		{32, 3, 28, 28},   // Small batch, typical MNIST with channels
		{128, 3, 32, 32},  // Medium batch, CIFAR-10 size
		{256, 64, 14, 14}, // Large batch, typical after pooling
	}

	for _, shape := range sizes {
		size := 1
		for _, dim := range shape {
			size *= dim
		}

		b.Run(fmt.Sprintf("shape_%dx%dx%dx%d", shape[0], shape[1], shape[2], shape[3]), func(b *testing.B) {
			// Create input tensor
			data := make([]float64, size)
			for i := range data {
				data[i] = float64(i) * 0.01
			}
			input := utils.NewTensorFromData(data, shape...)
			flatten := NewFlatten()

			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				output := flatten.Forward(input, false)
				_ = flatten.Backward(output)
			}
		})
	}
}