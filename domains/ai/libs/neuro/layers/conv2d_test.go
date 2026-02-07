package layers

import (
	"fmt"
	"math"
	"testing"

	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
)

func TestConv2DForwardSimple(t *testing.T) {
	// Simple test with known filter
	// Input: 1x1x3x3, Filter: 1x1x2x2 (edge detection)
	inputData := []float64{
		1, 2, 3,
		4, 5, 6,
		7, 8, 9,
	}
	input := utils.NewTensorFromData(inputData, 1, 1, 3, 3)

	// Create Conv2D layer
	conv := NewConv2D(1, 1, []int{2, 2}, 1, "valid", false)
	
	// Set known weights (edge detection kernel)
	weights := utils.NewTensorFromData([]float64{
		-1, 0,
		0, 1,
	}, 1, 1, 2, 2)
	conv.SetParams([]*utils.Tensor{weights})

	// Forward pass
	output := conv.Forward(input, false)

	// Check output shape (1, 1, 2, 2)
	expectedShape := []int{1, 1, 2, 2}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}

	// Check output values
	// Convolution results:
	// Position (0,0): -1*1 + 0*2 + 0*4 + 1*5 = 4
	// Position (0,1): -1*2 + 0*3 + 0*5 + 1*6 = 4
	// Position (1,0): -1*4 + 0*5 + 0*7 + 1*8 = 4
	// Position (1,1): -1*5 + 0*6 + 0*8 + 1*9 = 4
	expectedData := []float64{4, 4, 4, 4}
	for i, expected := range expectedData {
		if math.Abs(output.Data[i]-expected) > 1e-6 {
			t.Errorf("Data[%d]: expected %f, got %f", i, expected, output.Data[i])
		}
	}
}

func TestConv2DForwardWithBias(t *testing.T) {
	// Test convolution with bias
	inputData := []float64{
		1, 1,
		1, 1,
	}
	input := utils.NewTensorFromData(inputData, 1, 1, 2, 2)

	// Create Conv2D with bias
	conv := NewConv2D(1, 1, []int{2, 2}, 1, "valid", true)
	
	// Set weights and bias
	weights := utils.NewTensorFromData([]float64{1, 1, 1, 1}, 1, 1, 2, 2)
	bias := utils.NewTensorFromData([]float64{10}, 1)
	conv.SetParams([]*utils.Tensor{weights, bias})

	// Forward pass
	output := conv.Forward(input, false)

	// Check output (should be sum of all inputs + bias = 4 + 10 = 14)
	if math.Abs(output.Data[0]-14.0) > 1e-6 {
		t.Errorf("Expected 14.0, got %f", output.Data[0])
	}
}

func TestConv2DMultiChannel(t *testing.T) {
	// Test with multiple input channels
	// Input: 1x2x3x3 (2 channels)
	inputData := make([]float64, 18)
	for i := range inputData {
		inputData[i] = float64(i + 1)
	}
	input := utils.NewTensorFromData(inputData, 1, 2, 3, 3)

	// Create Conv2D: 2 input channels, 3 output channels
	conv := NewConv2D(2, 3, []int{2, 2}, 1, "valid", false)

	// Forward pass
	output := conv.Forward(input, false)

	// Check output shape (1, 3, 2, 2)
	expectedShape := []int{1, 3, 2, 2}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}
}

func TestConv2DSamePadding(t *testing.T) {
	// Test "same" padding
	input := utils.NewTensor(1, 1, 5, 5)
	for i := range input.Data {
		input.Data[i] = float64(i + 1)
	}

	// Create Conv2D with "same" padding
	conv := NewConv2D(1, 1, []int{3, 3}, 1, "same", false)

	// Forward pass
	output := conv.Forward(input, false)

	// With "same" padding and stride 1, output should have same spatial dimensions
	if output.Shape[2] != 5 || output.Shape[3] != 5 {
		t.Errorf("Expected output shape (1, 1, 5, 5), got %v", output.Shape)
	}
}

func TestConv2DStride(t *testing.T) {
	// Test with stride > 1
	input := utils.NewTensor(1, 1, 4, 4)
	for i := range input.Data {
		input.Data[i] = float64(i + 1)
	}

	// Create Conv2D with stride 2
	conv := NewConv2D(1, 1, []int{2, 2}, 2, "valid", false)

	// Forward pass
	output := conv.Forward(input, false)

	// With 4x4 input, 2x2 kernel, stride 2, output should be 2x2
	expectedShape := []int{1, 1, 2, 2}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}
}

func TestConv2DBackward(t *testing.T) {
	// Test backward pass
	input := utils.NewTensor(1, 1, 3, 3)
	for i := range input.Data {
		input.Data[i] = float64(i + 1)
	}

	// Create Conv2D
	conv := NewConv2D(1, 1, []int{2, 2}, 1, "valid", true)

	// Forward pass
	output := conv.Forward(input, true)

	// Create gradient output
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 1.0
	}

	// Backward pass
	gradInput := conv.Backward(gradOutput)

	// Check gradient input shape matches original input
	for i, dim := range input.Shape {
		if gradInput.Shape[i] != dim {
			t.Errorf("GradInput shape[%d]: expected %d, got %d", i, dim, gradInput.Shape[i])
		}
	}

	// Check that weight gradients are computed
	weightGradSum := 0.0
	for _, v := range conv.gradWeights.Data {
		weightGradSum += math.Abs(v)
	}
	if weightGradSum == 0 {
		t.Error("Weight gradients are all zero")
	}

	// Check that bias gradients are computed
	biasGradSum := 0.0
	for _, v := range conv.gradBias.Data {
		biasGradSum += math.Abs(v)
	}
	if biasGradSum == 0 {
		t.Error("Bias gradients are all zero")
	}
}

func TestConv2DUpdateWeights(t *testing.T) {
	// Test weight update
	input := utils.NewTensor(1, 1, 2, 2)
	for i := range input.Data {
		input.Data[i] = 1.0
	}

	conv := NewConv2D(1, 1, []int{2, 2}, 1, "valid", true)
	
	// Store initial weights
	initialWeights := make([]float64, len(conv.weights.Data))
	copy(initialWeights, conv.weights.Data)
	initialBias := conv.bias.Data[0]

	// Forward and backward
	output := conv.Forward(input, true)
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 1.0
	}
	conv.Backward(gradOutput)

	// Update weights
	lr := 0.01
	conv.UpdateWeights(lr)

	// Check weights changed
	weightsChanged := false
	for i := range conv.weights.Data {
		if conv.weights.Data[i] != initialWeights[i] {
			weightsChanged = true
			break
		}
	}
	if !weightsChanged {
		t.Error("Weights did not change after update")
	}

	// Check bias changed
	if conv.bias.Data[0] == initialBias {
		t.Error("Bias did not change after update")
	}

	// Check gradients were reset
	for _, v := range conv.gradWeights.Data {
		if v != 0 {
			t.Error("Weight gradients not reset after update")
		}
	}
	for _, v := range conv.gradBias.Data {
		if v != 0 {
			t.Error("Bias gradients not reset after update")
		}
	}
}

func TestConv2DBatch(t *testing.T) {
	// Test with batch size > 1
	batchSize := 2
	input := utils.NewTensor(batchSize, 1, 3, 3)
	for i := range input.Data {
		input.Data[i] = float64(i + 1)
	}

	conv := NewConv2D(1, 2, []int{2, 2}, 1, "valid", false)

	// Forward pass
	output := conv.Forward(input, false)

	// Check output shape
	expectedShape := []int{batchSize, 2, 2, 2}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}
}

func TestConv2DGradientCheck(t *testing.T) {
	// Numerical gradient checking
	epsilon := 1e-5
	
	// Small input for faster computation
	input := utils.NewTensor(1, 1, 3, 3)
	for i := range input.Data {
		input.Data[i] = float64(i+1) * 0.1
	}

	conv := NewConv2D(1, 1, []int{2, 2}, 1, "valid", true)

	// Forward pass
	output := conv.Forward(input, true)
	
	// Create gradient output
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 1.0
	}

	// Backward pass for analytical gradients
	conv.Backward(gradOutput)

	// Check weight gradients numerically
	for i := 0; i < len(conv.weights.Data); i++ {
		// Save original weight
		origWeight := conv.weights.Data[i]

		// Forward with weight + epsilon
		conv.weights.Data[i] = origWeight + epsilon
		outputPlus := conv.Forward(input, false)
		lossPlus := 0.0
		for j := range outputPlus.Data {
			lossPlus += outputPlus.Data[j]
		}

		// Forward with weight - epsilon
		conv.weights.Data[i] = origWeight - epsilon
		outputMinus := conv.Forward(input, false)
		lossMinus := 0.0
		for j := range outputMinus.Data {
			lossMinus += outputMinus.Data[j]
		}

		// Restore original weight
		conv.weights.Data[i] = origWeight

		// Numerical gradient
		numericalGrad := (lossPlus - lossMinus) / (2 * epsilon)
		analyticalGrad := conv.gradWeights.Data[i]

		// Check if gradients match (with some tolerance)
		relError := math.Abs(numericalGrad-analyticalGrad) / (math.Abs(numericalGrad) + math.Abs(analyticalGrad) + 1e-8)
		if relError > 0.01 { // 1% tolerance
			t.Errorf("Weight gradient[%d] mismatch: numerical=%f, analytical=%f, rel_error=%f", 
				i, numericalGrad, analyticalGrad, relError)
		}
	}
}

func TestConv2DNegativeValues(t *testing.T) {
	// Test with negative input values
	inputData := []float64{
		-1, -2, -3,
		-4, -5, -6,
		-7, -8, -9,
	}
	input := utils.NewTensorFromData(inputData, 1, 1, 3, 3)

	conv := NewConv2D(1, 1, []int{2, 2}, 1, "valid", true)
	
	// Set known weights
	weights := utils.NewTensorFromData([]float64{1, -1, -1, 1}, 1, 1, 2, 2)
	bias := utils.NewTensorFromData([]float64{5}, 1)
	conv.SetParams([]*utils.Tensor{weights, bias})

	// Forward pass should handle negative values correctly
	output := conv.Forward(input, false)

	// Check output shape
	expectedShape := []int{1, 1, 2, 2}
	for i, dim := range expectedShape {
		if output.Shape[i] != dim {
			t.Errorf("Shape[%d]: expected %d, got %d", i, dim, output.Shape[i])
		}
	}

	// Verify computation with negative values
	// Position (0,0): 1*(-1) + (-1)*(-2) + (-1)*(-4) + 1*(-5) = -1 + 2 + 4 - 5 = 0 + bias = 5
	expectedData := []float64{5, 5, 5, 5}
	for i, expected := range expectedData {
		if math.Abs(output.Data[i]-expected) > 1e-6 {
			t.Errorf("Data[%d]: expected %f, got %f", i, expected, output.Data[i])
		}
	}
}

func TestConv2DLargeTensor(t *testing.T) {
	// Test with larger tensors to check memory handling
	batch, channels, height, width := 4, 8, 64, 64
	input := utils.NewTensor(batch, channels, height, width)
	
	// Initialize with pattern
	for i := range input.Data {
		input.Data[i] = float64(i % 100) * 0.01
	}

	conv := NewConv2D(8, 16, []int{3, 3}, 2, "same", true)
	
	// Should not panic or cause memory issues
	output := conv.Forward(input, true)
	
	// Check output dimensions with stride 2 and same padding
	expectedH := (height + 2 - 1) / 2  // ceil(64/2) = 32
	expectedW := (width + 2 - 1) / 2   // ceil(64/2) = 32
	
	if output.Shape[0] != batch || output.Shape[1] != 16 || 
	   output.Shape[2] != expectedH || output.Shape[3] != expectedW {
		t.Errorf("Expected shape (%d, 16, %d, %d), got %v", 
			batch, expectedH, expectedW, output.Shape)
	}

	// Test backward pass with large tensor
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.01
	}
	
	gradInput := conv.Backward(gradOutput)
	if gradInput.Shape[0] != batch || gradInput.Shape[1] != channels ||
	   gradInput.Shape[2] != height || gradInput.Shape[3] != width {
		t.Errorf("Gradient shape mismatch: expected (%d, %d, %d, %d), got %v",
			batch, channels, height, width, gradInput.Shape)
	}
}

func TestConv2DZeroInput(t *testing.T) {
	// Test with all-zero input
	input := utils.NewTensor(1, 1, 3, 3)
	// Data is already zero-initialized

	conv := NewConv2D(1, 1, []int{2, 2}, 1, "valid", true)
	output := conv.Forward(input, false)

	// With zero input and bias, output should be just the bias value
	biasValue := conv.bias.Data[0]
	for i := range output.Data {
		if math.Abs(output.Data[i]-biasValue) > 1e-6 {
			t.Errorf("Expected all outputs to be bias value %f, got %f at position %d",
				biasValue, output.Data[i], i)
		}
	}
}

func TestConv2DInvalidInputPanics(t *testing.T) {
	conv := NewConv2D(3, 8, []int{3, 3}, 1, "valid", false)

	// Test with wrong number of dimensions
	defer func() {
		if r := recover(); r == nil {
			t.Error("Expected panic for 3D input")
		}
	}()
	
	wrongDimInput := utils.NewTensor(2, 3, 28)
	conv.Forward(wrongDimInput, false)
}

func TestConv2DChannelMismatch(t *testing.T) {
	conv := NewConv2D(3, 8, []int{3, 3}, 1, "valid", false)

	// Test with wrong number of input channels
	defer func() {
		if r := recover(); r == nil {
			t.Error("Expected panic for channel mismatch")
		}
	}()
	
	wrongChannelInput := utils.NewTensor(1, 5, 28, 28)  // 5 channels instead of 3
	conv.Forward(wrongChannelInput, false)
}

func TestConv2DConstructorValidation(t *testing.T) {
	tests := []struct {
		name        string
		inChannels  int
		outChannels int
		kernelSize  []int
		stride      int
		padding     string
		shouldPanic bool
		errorMsg    string
	}{
		{
			name:        "Invalid kernel size length",
			inChannels:  3, outChannels: 8,
			kernelSize:  []int{3}, // Should be 2 elements
			stride:      1, padding: "valid",
			shouldPanic: true,
			errorMsg:    "kernelSize must have 2 elements",
		},
		{
			name:        "Zero stride",
			inChannels:  3, outChannels: 8,
			kernelSize:  []int{3, 3},
			stride:      0, // Invalid
			padding:     "valid",
			shouldPanic: true,
			errorMsg:    "stride must be positive",
		},
		{
			name:        "Negative stride",
			inChannels:  3, outChannels: 8,
			kernelSize:  []int{3, 3},
			stride:      -1, // Invalid
			padding:     "valid",
			shouldPanic: true,
			errorMsg:    "stride must be positive",
		},
		{
			name:        "Invalid padding type",
			inChannels:  3, outChannels: 8,
			kernelSize:  []int{3, 3},
			stride:      1,
			padding:     "invalid_padding",
			shouldPanic: true,
			errorMsg:    "padding must be 'valid' or 'same'",
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if tc.shouldPanic {
				defer func() {
					if r := recover(); r == nil {
						t.Errorf("Expected panic with message containing '%s'", tc.errorMsg)
					}
				}()
			}
			
			_ = NewConv2D(tc.inChannels, tc.outChannels, tc.kernelSize, 
				tc.stride, tc.padding, false)
			
			if tc.shouldPanic {
				t.Error("Should have panicked but didn't")
			}
		})
	}
}

func TestConv2DWeightGradientAccumulation(t *testing.T) {
	// Test that gradients accumulate across multiple backward passes
	input1 := utils.NewTensor(1, 1, 3, 3)
	input2 := utils.NewTensor(1, 1, 3, 3)
	
	for i := range input1.Data {
		input1.Data[i] = float64(i) * 0.1
		input2.Data[i] = float64(i) * 0.2
	}

	conv := NewConv2D(1, 1, []int{2, 2}, 1, "valid", true)
	
	// First forward-backward pass
	output1 := conv.Forward(input1, true)
	gradOutput1 := utils.NewTensor(output1.Shape...)
	for i := range gradOutput1.Data {
		gradOutput1.Data[i] = 1.0
	}
	conv.Backward(gradOutput1)
	
	// Store first gradient
	firstGradWeights := make([]float64, len(conv.gradWeights.Data))
	copy(firstGradWeights, conv.gradWeights.Data)
	
	// Second forward-backward pass (without weight update)
	output2 := conv.Forward(input2, true)
	gradOutput2 := utils.NewTensor(output2.Shape...)
	for i := range gradOutput2.Data {
		gradOutput2.Data[i] = 1.0
	}
	conv.Backward(gradOutput2)
	
	// Check that gradients accumulated
	accumulated := false
	for i := range conv.gradWeights.Data {
		if conv.gradWeights.Data[i] != firstGradWeights[i] {
			accumulated = true
			break
		}
	}
	
	if !accumulated {
		t.Error("Weight gradients should accumulate across backward passes")
	}
	
	// After update, gradients should be reset
	conv.UpdateWeights(0.01)
	for i := range conv.gradWeights.Data {
		if conv.gradWeights.Data[i] != 0 {
			t.Errorf("Weight gradient at position %d not reset after update: %f", 
				i, conv.gradWeights.Data[i])
		}
	}
}

func BenchmarkConv2D(b *testing.B) {
	sizes := []struct {
		batch, inCh, outCh, height, width int
		kernelSize                        []int
		stride                            int
	}{
		{32, 3, 32, 28, 28, []int{3, 3}, 1},   // First conv layer
		{64, 32, 64, 14, 14, []int{3, 3}, 1},  // Middle layer
		{128, 64, 128, 7, 7, []int{3, 3}, 1},  // Deep layer
		{256, 3, 64, 32, 32, []int{5, 5}, 2},  // Large kernel and stride
	}

	for _, s := range sizes {
		name := fmt.Sprintf("b%d_i%d_o%d_h%d_w%d_k%dx%d_s%d",
			s.batch, s.inCh, s.outCh, s.height, s.width, 
			s.kernelSize[0], s.kernelSize[1], s.stride)
		b.Run(name, func(b *testing.B) {
			input := utils.NewTensor(s.batch, s.inCh, s.height, s.width)
			for i := range input.Data {
				input.Data[i] = float64(i) * 0.01
			}
			conv := NewConv2D(s.inCh, s.outCh, s.kernelSize, s.stride, "valid", true)

			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				output := conv.Forward(input, true)
				gradOutput := utils.NewTensor(output.Shape...)
				for j := range gradOutput.Data {
					gradOutput.Data[j] = 0.01
				}
				_ = conv.Backward(gradOutput)
			}
		})
	}
}