package layers

import (
	"fmt"
	"math"
	"testing"

	"github.com/muchq/moonbase/go/neuro/utils"
)

func TestMaxPool2DForward(t *testing.T) {
	tests := []struct {
		name         string
		inputData    []float64
		inputShape   []int
		poolSize     []int
		stride       int
		padding      string
		expectedData []float64
		outputShape  []int
	}{
		{
			name: "2x2 pool, stride 2, no padding",
			inputData: []float64{
				1, 2, 3, 4,
				5, 6, 7, 8,
				9, 10, 11, 12,
				13, 14, 15, 16,
			},
			inputShape:   []int{1, 1, 4, 4},
			poolSize:     []int{2, 2},
			stride:       2,
			padding:      "valid",
			expectedData: []float64{6, 8, 14, 16},
			outputShape:  []int{1, 1, 2, 2},
		},
		{
			name: "3x3 pool, stride 1, no padding",
			inputData: []float64{
				1, 2, 3, 4, 5,
				6, 7, 8, 9, 10,
				11, 12, 13, 14, 15,
				16, 17, 18, 19, 20,
				21, 22, 23, 24, 25,
			},
			inputShape:   []int{1, 1, 5, 5},
			poolSize:     []int{3, 3},
			stride:       1,
			padding:      "valid",
			expectedData: []float64{13, 14, 15, 18, 19, 20, 23, 24, 25},
			outputShape:  []int{1, 1, 3, 3},
		},
		{
			name: "Multi-channel pooling",
			inputData: []float64{
				// Channel 0
				1, 2, 3, 4,
				5, 6, 7, 8,
				9, 10, 11, 12,
				13, 14, 15, 16,
				// Channel 1
				16, 15, 14, 13,
				12, 11, 10, 9,
				8, 7, 6, 5,
				4, 3, 2, 1,
			},
			inputShape: []int{1, 2, 4, 4},
			poolSize:   []int{2, 2},
			stride:     2,
			padding:    "valid",
			expectedData: []float64{
				6, 8, 14, 16,    // Channel 0 max values
				16, 14, 8, 6,    // Channel 1 max values
			},
			outputShape: []int{1, 2, 2, 2},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			input := utils.NewTensorFromData(tc.inputData, tc.inputShape...)
			pool := NewMaxPool2D(tc.poolSize, tc.stride, tc.padding)
			output := pool.Forward(input, false)

			// Check output shape
			for i, dim := range tc.outputShape {
				if output.Shape[i] != dim {
					t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
				}
			}

			// Check output values
			for i, expected := range tc.expectedData {
				if math.Abs(output.Data[i]-expected) > 1e-9 {
					t.Errorf("Data[%d]: expected %f, got %f", i, expected, output.Data[i])
				}
			}
		})
	}
}

func TestMaxPool2DBackward(t *testing.T) {
	// Create input
	inputData := []float64{
		1, 2, 3, 4,
		5, 6, 7, 8,
		9, 10, 11, 12,
		13, 14, 15, 16,
	}
	input := utils.NewTensorFromData(inputData, 1, 1, 4, 4)

	// Forward pass
	pool := NewMaxPool2D([]int{2, 2}, 2, "valid")
	output := pool.Forward(input, true)

	// Create gradient for output
	gradOutputData := []float64{1, 2, 3, 4}
	gradOutput := utils.NewTensorFromData(gradOutputData, output.Shape...)

	// Backward pass
	gradInput := pool.Backward(gradOutput)

	// Check gradient shape
	for i, dim := range input.Shape {
		if gradInput.Shape[i] != dim {
			t.Errorf("Gradient shape[%d]: expected %d, got %d", i, dim, gradInput.Shape[i])
		}
	}

	// Check that gradients are routed to max positions
	// Max positions were at indices: 5(6), 7(8), 13(14), 15(16)
	expectedGrad := []float64{
		0, 0, 0, 0,
		0, 1, 0, 2,
		0, 0, 0, 0,
		0, 3, 0, 4,
	}

	for i, expected := range expectedGrad {
		if math.Abs(gradInput.Data[i]-expected) > 1e-9 {
			t.Errorf("Gradient[%d]: expected %f, got %f", i, expected, gradInput.Data[i])
		}
	}
}

func TestMaxPool2DSamePadding(t *testing.T) {
	// Create 5x5 input
	inputData := make([]float64, 25)
	for i := range inputData {
		inputData[i] = float64(i + 1)
	}
	input := utils.NewTensorFromData(inputData, 1, 1, 5, 5)

	// Apply 2x2 pooling with stride 2 and "same" padding
	pool := NewMaxPool2D([]int{2, 2}, 2, "same")
	output := pool.Forward(input, false)

	// With "same" padding, stride 2, and kernel 2x2:
	// Output should be ceil(5/2) = 3x3 to match TensorFlow/Keras behavior
	expectedShape := []int{1, 1, 3, 3}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}

	// Check expected max values for 3x3 output with asymmetric padding
	// For 5x5 input with 2x2 kernel and stride 2:
	// Total padding needed = (3-1)*2 + 2 - 5 = 4 + 2 - 5 = 1
	// With asymmetric padding: padTop=0, padBottom=1, padLeft=0, padRight=1
	// The padded input becomes:
	//  1  2  3  4  5  0
	//  6  7  8  9 10  0
	// 11 12 13 14 15  0
	// 16 17 18 19 20  0
	// 21 22 23 24 25  0
	//  0  0  0  0  0  0
	// 
	// With 2x2 pooling, stride 2:
	// Position (0,0): starts at (0,0), covers [1,2,6,7] -> max = 7
	// Position (0,1): starts at (0,2), covers [3,4,8,9] -> max = 9
	// Position (0,2): starts at (0,4), covers [5,0,10,0] -> max = 10
	// Position (1,0): starts at (2,0), covers [11,12,16,17] -> max = 17
	// Position (1,1): starts at (2,2), covers [13,14,18,19] -> max = 19
	// Position (1,2): starts at (2,4), covers [15,0,20,0] -> max = 20
	// Position (2,0): starts at (4,0), covers [21,22,0,0] -> max = 22
	// Position (2,1): starts at (4,2), covers [23,24,0,0] -> max = 24
	// Position (2,2): starts at (4,4), covers [25,0,0,0] -> max = 25
	if output.Shape[2] == 3 && output.Shape[3] == 3 {
		expectedValues := []float64{
			7, 9, 10,     // Row 1
			17, 19, 20,   // Row 2  
			22, 24, 25,   // Row 3
		}
		for i, expected := range expectedValues {
			if math.Abs(output.Data[i]-expected) > 1e-9 {
				t.Errorf("Output[%d]: expected %f, got %f", i, expected, output.Data[i])
			}
		}
	}
}

func TestMaxPool2DOverlapping(t *testing.T) {
	// Test overlapping pooling windows (stride < pool size)
	inputData := []float64{
		1, 2, 3, 4,
		5, 6, 7, 8,
		9, 10, 11, 12,
		13, 14, 15, 16,
	}
	input := utils.NewTensorFromData(inputData, 1, 1, 4, 4)

	pool := NewMaxPool2D([]int{3, 3}, 1, "valid")
	output := pool.Forward(input, false)

	// Output should be 2x2
	expectedShape := []int{1, 1, 2, 2}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}

	// Check max values
	expectedData := []float64{11, 12, 15, 16}
	for i, expected := range expectedData {
		if math.Abs(output.Data[i]-expected) > 1e-9 {
			t.Errorf("Data[%d]: expected %f, got %f", i, expected, output.Data[i])
		}
	}
}

func TestMaxPool2DBatch(t *testing.T) {
	// Test with batch size > 1
	batchSize := 2
	inputData := make([]float64, batchSize*1*4*4)
	for i := range inputData {
		inputData[i] = float64(i + 1)
	}
	input := utils.NewTensorFromData(inputData, batchSize, 1, 4, 4)

	pool := NewMaxPool2D([]int{2, 2}, 2, "valid")
	output := pool.Forward(input, false)

	// Check output shape
	expectedShape := []int{2, 1, 2, 2}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}

	// Check that each batch is processed independently
	// First batch max values
	batch0Expected := []float64{6, 8, 14, 16}
	for i, expected := range batch0Expected {
		if math.Abs(output.Data[i]-expected) > 1e-9 {
			t.Errorf("Batch 0, Data[%d]: expected %f, got %f", i, expected, output.Data[i])
		}
	}
}

func TestMaxPool2DNoParams(t *testing.T) {
	pool := NewMaxPool2D([]int{2, 2}, 1, "valid")

	// Test GetParams returns empty
	params := pool.GetParams()
	if len(params) != 0 {
		t.Errorf("Expected 0 parameters, got %d", len(params))
	}

	// Test SetParams with empty slice works
	pool.SetParams([]*utils.Tensor{})

	// Test SetParams with non-empty slice panics
	defer func() {
		if r := recover(); r == nil {
			t.Error("Expected panic when setting parameters on MaxPool2D layer")
		}
	}()
	pool.SetParams([]*utils.Tensor{utils.NewTensor(1, 1)})
}

func TestMaxPool2DBackwardBeforeForward(t *testing.T) {
	pool := NewMaxPool2D([]int{2, 2}, 1, "valid")
	gradOutput := utils.NewTensor(1, 1, 2, 2)

	defer func() {
		if r := recover(); r == nil {
			t.Error("Expected panic when calling Backward before Forward")
		}
	}()
	pool.Backward(gradOutput)
}

func TestMaxPool2DNegativeValues(t *testing.T) {
	// Test max pooling with negative values
	inputData := []float64{
		-9, -8, -7, -6,
		-5, -4, -3, -2,
		-1,  0,  1,  2,
		 3,  4,  5,  6,
	}
	input := utils.NewTensorFromData(inputData, 1, 1, 4, 4)

	pool := NewMaxPool2D([]int{2, 2}, 2, "valid")
	output := pool.Forward(input, false)

	// Check that max pooling correctly selects least negative/most positive values
	expectedData := []float64{-4, -2, 4, 6}
	for i, expected := range expectedData {
		if math.Abs(output.Data[i]-expected) > 1e-9 {
			t.Errorf("Data[%d]: expected %f, got %f", i, expected, output.Data[i])
		}
	}
}

func TestMaxPool2DAllEqualValues(t *testing.T) {
	// Test with all equal values (tie-breaking scenario)
	input := utils.NewTensor(1, 1, 4, 4)
	for i := range input.Data {
		input.Data[i] = 5.0
	}

	pool := NewMaxPool2D([]int{2, 2}, 2, "valid")
	output := pool.Forward(input, true)

	// All outputs should be 5.0
	for i := range output.Data {
		if math.Abs(output.Data[i]-5.0) > 1e-9 {
			t.Errorf("Data[%d]: expected 5.0, got %f", i, output.Data[i])
		}
	}

	// Test backward pass - gradient should be distributed to first max position
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 1.0
	}
	gradInput := pool.Backward(gradOutput)

	// Each 2x2 block should have exactly one gradient of 1.0
	gradSum := 0.0
	for _, v := range gradInput.Data {
		gradSum += v
	}
	if math.Abs(gradSum-4.0) > 1e-9 {
		t.Errorf("Expected total gradient sum of 4.0, got %f", gradSum)
	}
}

func TestMaxPool2DSingleElement(t *testing.T) {
	// Test with 1x1 spatial dimensions
	input := utils.NewTensorFromData([]float64{42.0}, 1, 1, 1, 1)
	
	pool := NewMaxPool2D([]int{1, 1}, 1, "valid")
	output := pool.Forward(input, false)

	// Output should be identical to input
	if output.Shape[2] != 1 || output.Shape[3] != 1 {
		t.Errorf("Expected shape (1, 1, 1, 1), got %v", output.Shape)
	}
	if math.Abs(output.Data[0]-42.0) > 1e-9 {
		t.Errorf("Expected 42.0, got %f", output.Data[0])
	}
}

func TestMaxPool2DLargeKernel(t *testing.T) {
	// Test with kernel size larger than input
	input := utils.NewTensor(1, 1, 3, 3)
	for i := range input.Data {
		input.Data[i] = float64(i + 1)
	}

	pool := NewMaxPool2D([]int{5, 5}, 1, "same")
	output := pool.Forward(input, false)

	// With padding, should still produce valid output
	// The max value should be 9 (the largest in the input)
	maxVal := 0.0
	for _, v := range output.Data {
		if v > maxVal {
			maxVal = v
		}
	}
	if math.Abs(maxVal-9.0) > 1e-9 {
		t.Errorf("Expected max value 9.0, got %f", maxVal)
	}
}

func TestMaxPool2DConstructorValidation(t *testing.T) {
	tests := []struct {
		name        string
		poolSize    []int
		stride      int
		padding     string
		shouldPanic bool
	}{
		{
			name:        "Invalid pool size length",
			poolSize:    []int{3},
			stride:      1,
			padding:     "valid",
			shouldPanic: true,
		},
		{
			name:        "Zero stride",
			poolSize:    []int{2, 2},
			stride:      0,
			padding:     "valid",
			shouldPanic: true,
		},
		{
			name:        "Negative stride",
			poolSize:    []int{2, 2},
			stride:      -1,
			padding:     "valid",
			shouldPanic: true,
		},
		{
			name:        "Invalid padding",
			poolSize:    []int{2, 2},
			stride:      1,
			padding:     "reflect",
			shouldPanic: true,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if tc.shouldPanic {
				defer func() {
					if r := recover(); r == nil {
						t.Error("Expected panic but didn't get one")
					}
				}()
			}
			
			_ = NewMaxPool2D(tc.poolSize, tc.stride, tc.padding)
			
			if tc.shouldPanic {
				t.Error("Should have panicked but didn't")
			}
		})
	}
}

func TestMaxPool2DNumericalGradient(t *testing.T) {
	// Numerical gradient checking for MaxPool2D
	epsilon := 1e-5
	
	// Small input for faster computation
	input := utils.NewTensor(1, 2, 4, 4)
	for i := range input.Data {
		input.Data[i] = float64(i+1) * 0.1
	}

	pool := NewMaxPool2D([]int{2, 2}, 2, "valid")

	// Forward pass
	output := pool.Forward(input, true)
	
	// Create gradient output (simulate loss gradient)
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 1.0
	}

	// Analytical gradient via backward pass
	gradInputAnalytical := pool.Backward(gradOutput)

	// Numerical gradient checking
	gradInputNumerical := utils.NewTensor(input.Shape...)
	
	for i := 0; i < len(input.Data); i++ {
		// Save original value
		origVal := input.Data[i]
		
		// Forward with input + epsilon
		input.Data[i] = origVal + epsilon
		outputPlus := pool.Forward(input, false)
		lossPlus := 0.0
		for j := range outputPlus.Data {
			lossPlus += outputPlus.Data[j]
		}
		
		// Forward with input - epsilon
		input.Data[i] = origVal - epsilon
		outputMinus := pool.Forward(input, false)
		lossMinus := 0.0
		for j := range outputMinus.Data {
			lossMinus += outputMinus.Data[j]
		}
		
		// Restore original value
		input.Data[i] = origVal
		
		// Numerical gradient
		gradInputNumerical.Data[i] = (lossPlus - lossMinus) / (2 * epsilon)
	}
	
	// Compare analytical and numerical gradients
	for i := range gradInputAnalytical.Data {
		diff := math.Abs(gradInputAnalytical.Data[i] - gradInputNumerical.Data[i])
		// MaxPool has discrete gradient (0 or 1), so we allow some tolerance
		if diff > 0.01 && math.Abs(gradInputAnalytical.Data[i]) > 1e-6 {
			t.Errorf("Gradient mismatch at position %d: analytical=%f, numerical=%f, diff=%f",
				i, gradInputAnalytical.Data[i], gradInputNumerical.Data[i], diff)
		}
	}
}

func TestMaxPool2DMultiChannelGradient(t *testing.T) {
	// Test gradient flow with multiple channels
	input := utils.NewTensor(2, 3, 4, 4)
	for i := range input.Data {
		input.Data[i] = float64(i) * 0.01
	}

	pool := NewMaxPool2D([]int{2, 2}, 2, "valid")
	output := pool.Forward(input, true)

	// Create gradient
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = float64(i) * 0.1
	}

	gradInput := pool.Backward(gradOutput)

	// Check shape
	for i := range input.Shape {
		if gradInput.Shape[i] != input.Shape[i] {
			t.Errorf("Gradient shape[%d]: expected %d, got %d", 
				i, input.Shape[i], gradInput.Shape[i])
		}
	}

	// Verify gradient sum per channel matches roughly
	// (each output gradient should appear exactly once in input gradient)
	inputGradSum := 0.0
	outputGradSum := 0.0
	for _, v := range gradInput.Data {
		inputGradSum += v
	}
	for _, v := range gradOutput.Data {
		outputGradSum += v
	}
	if math.Abs(inputGradSum-outputGradSum) > 1e-6 {
		t.Errorf("Gradient sum mismatch: input=%f, output=%f", 
			inputGradSum, outputGradSum)
	}
}

func BenchmarkMaxPool2D(b *testing.B) {
	sizes := []struct {
		batch, channels, height, width int
		poolSize                       []int
		stride                         int
	}{
		{32, 3, 28, 28, []int{2, 2}, 2},    // Small
		{64, 32, 14, 14, []int{2, 2}, 2},   // After conv
		{128, 64, 7, 7, []int{3, 3}, 1},    // Deep layer
		{256, 128, 14, 14, []int{2, 2}, 2}, // Large batch
	}

	for _, s := range sizes {
		name := fmt.Sprintf("b%d_c%d_h%d_w%d_p%dx%d_s%d",
			s.batch, s.channels, s.height, s.width, s.poolSize[0], s.poolSize[1], s.stride)
		b.Run(name, func(b *testing.B) {
			size := s.batch * s.channels * s.height * s.width
			data := make([]float64, size)
			for i := range data {
				data[i] = float64(i) * 0.01
			}
			input := utils.NewTensorFromData(data, s.batch, s.channels, s.height, s.width)
			pool := NewMaxPool2D(s.poolSize, s.stride, "valid")

			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				output := pool.Forward(input, false)
				_ = pool.Backward(output)
			}
		})
	}
}