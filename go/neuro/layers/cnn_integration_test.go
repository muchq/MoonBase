package layers

import (
	"math"
	"testing"

	"github.com/muchq/moonbase/go/neuro/utils"
)

// TestCNNIntegrationSimple tests a simple CNN: Conv2D -> MaxPool2D -> Flatten
func TestCNNIntegrationSimple(t *testing.T) {
	// Create input (batch=2, channels=1, height=8, width=8)
	batchSize := 2
	input := utils.NewTensor(batchSize, 1, 8, 8)
	for i := range input.Data {
		input.Data[i] = float64(i%10) * 0.1
	}

	// Build simple CNN
	conv := NewConv2D(1, 4, []int{3, 3}, 1, "valid", true)
	pool := NewMaxPool2D([]int{2, 2}, 2, "valid")
	flatten := NewFlatten()

	// Forward pass
	convOut := conv.Forward(input, true)
	
	// Check conv output shape: (2, 4, 6, 6) with valid padding
	if convOut.Shape[0] != batchSize || convOut.Shape[1] != 4 || 
	   convOut.Shape[2] != 6 || convOut.Shape[3] != 6 {
		t.Errorf("Conv output shape expected (2, 4, 6, 6), got %v", convOut.Shape)
	}

	poolOut := pool.Forward(convOut, true)
	
	// Check pool output shape: (2, 4, 3, 3) after 2x2 pooling with stride 2
	if poolOut.Shape[0] != batchSize || poolOut.Shape[1] != 4 || 
	   poolOut.Shape[2] != 3 || poolOut.Shape[3] != 3 {
		t.Errorf("Pool output shape expected (2, 4, 3, 3), got %v", poolOut.Shape)
	}

	flatOut := flatten.Forward(poolOut, true)
	
	// Check flatten output shape: (2, 36)
	if flatOut.Shape[0] != batchSize || flatOut.Shape[1] != 36 {
		t.Errorf("Flatten output shape expected (2, 36), got %v", flatOut.Shape)
	}

	// Test backward pass
	gradOutput := utils.NewTensor(flatOut.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.01
	}

	// Backward through flatten
	gradPool := flatten.Backward(gradOutput)
	if gradPool.Shape[0] != poolOut.Shape[0] || gradPool.Shape[1] != poolOut.Shape[1] ||
	   gradPool.Shape[2] != poolOut.Shape[2] || gradPool.Shape[3] != poolOut.Shape[3] {
		t.Errorf("Flatten backward shape mismatch: expected %v, got %v", poolOut.Shape, gradPool.Shape)
	}

	// Backward through pool
	gradConv := pool.Backward(gradPool)
	if gradConv.Shape[0] != convOut.Shape[0] || gradConv.Shape[1] != convOut.Shape[1] ||
	   gradConv.Shape[2] != convOut.Shape[2] || gradConv.Shape[3] != convOut.Shape[3] {
		t.Errorf("Pool backward shape mismatch: expected %v, got %v", convOut.Shape, gradConv.Shape)
	}

	// Backward through conv
	gradInput := conv.Backward(gradConv)
	if gradInput.Shape[0] != input.Shape[0] || gradInput.Shape[1] != input.Shape[1] ||
	   gradInput.Shape[2] != input.Shape[2] || gradInput.Shape[3] != input.Shape[3] {
		t.Errorf("Conv backward shape mismatch: expected %v, got %v", input.Shape, gradInput.Shape)
	}

	// Verify gradients flow (should be non-zero)
	gradSum := 0.0
	for _, v := range gradInput.Data {
		gradSum += math.Abs(v)
	}
	if gradSum == 0 {
		t.Error("No gradient flow detected through CNN")
	}
}

// TestCNNIntegrationMultiLayer tests a deeper CNN with multiple conv/pool layers
func TestCNNIntegrationMultiLayer(t *testing.T) {
	// Create input (batch=4, channels=3, height=32, width=32) - like CIFAR-10
	batchSize := 4
	input := utils.NewTensor(batchSize, 3, 32, 32)
	for i := range input.Data {
		input.Data[i] = float64(i%100) * 0.001
	}

	// Build CNN: Conv -> Pool -> Conv -> Pool -> Flatten
	conv1 := NewConv2D(3, 16, []int{3, 3}, 1, "same", true)
	pool1 := NewMaxPool2D([]int{2, 2}, 2, "valid")
	conv2 := NewConv2D(16, 32, []int{3, 3}, 1, "same", true)
	pool2 := NewMaxPool2D([]int{2, 2}, 2, "valid")
	flatten := NewFlatten()

	// Forward pass
	out1 := conv1.Forward(input, true)
	// After conv1 with same padding: (4, 16, 32, 32)
	if out1.Shape[1] != 16 || out1.Shape[2] != 32 || out1.Shape[3] != 32 {
		t.Errorf("Conv1 output shape error: got %v", out1.Shape)
	}

	out2 := pool1.Forward(out1, true)
	// After pool1: (4, 16, 16, 16)
	if out2.Shape[1] != 16 || out2.Shape[2] != 16 || out2.Shape[3] != 16 {
		t.Errorf("Pool1 output shape error: got %v", out2.Shape)
	}

	out3 := conv2.Forward(out2, true)
	// After conv2 with same padding: (4, 32, 16, 16)
	if out3.Shape[1] != 32 || out3.Shape[2] != 16 || out3.Shape[3] != 16 {
		t.Errorf("Conv2 output shape error: got %v", out3.Shape)
	}

	out4 := pool2.Forward(out3, true)
	// After pool2: (4, 32, 8, 8)
	if out4.Shape[1] != 32 || out4.Shape[2] != 8 || out4.Shape[3] != 8 {
		t.Errorf("Pool2 output shape error: got %v", out4.Shape)
	}

	out5 := flatten.Forward(out4, true)
	// After flatten: (4, 2048)
	expectedFeatures := 32 * 8 * 8
	if out5.Shape[0] != batchSize || out5.Shape[1] != expectedFeatures {
		t.Errorf("Flatten output shape expected (%d, %d), got %v", 
			batchSize, expectedFeatures, out5.Shape)
	}

	// Test backward pass through entire network
	gradOutput := utils.NewTensor(out5.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.001
	}

	// Backward through all layers
	grad5 := flatten.Backward(gradOutput)
	grad4 := pool2.Backward(grad5)
	grad3 := conv2.Backward(grad4)
	grad2 := pool1.Backward(grad3)
	grad1 := conv1.Backward(grad2)

	// Final gradient should match input shape
	for i := range input.Shape {
		if grad1.Shape[i] != input.Shape[i] {
			t.Errorf("Final gradient shape[%d]: expected %d, got %d",
				i, input.Shape[i], grad1.Shape[i])
		}
	}

	// Check that conv layers accumulated gradients
	conv1GradSum := 0.0
	for _, v := range conv1.gradWeights.Data {
		conv1GradSum += math.Abs(v)
	}
	if conv1GradSum == 0 {
		t.Error("Conv1 weight gradients are zero")
	}

	conv2GradSum := 0.0
	for _, v := range conv2.gradWeights.Data {
		conv2GradSum += math.Abs(v)
	}
	if conv2GradSum == 0 {
		t.Error("Conv2 weight gradients are zero")
	}
}

// TestCNNIntegrationGradientFlow tests gradient flow through CNN with numerical checking
func TestCNNIntegrationGradientFlow(t *testing.T) {
	// Small network for numerical gradient checking
	input := utils.NewTensor(1, 1, 4, 4)
	for i := range input.Data {
		input.Data[i] = float64(i+1) * 0.1
	}

	// Simple CNN
	conv := NewConv2D(1, 2, []int{2, 2}, 1, "valid", true)
	pool := NewMaxPool2D([]int{2, 2}, 1, "valid")
	flatten := NewFlatten()

	// Function to compute loss (sum of outputs for simplicity)
	computeLoss := func(x *utils.Tensor) float64 {
		o1 := conv.Forward(x, false)
		o2 := pool.Forward(o1, false)
		o3 := flatten.Forward(o2, false)
		
		loss := 0.0
		for _, v := range o3.Data {
			loss += v
		}
		return loss
	}

	// Analytical gradient
	out1 := conv.Forward(input, true)
	out2 := pool.Forward(out1, true)
	out3 := flatten.Forward(out2, true)
	
	// Gradient is 1 for each output (since loss is sum)
	gradOutput := utils.NewTensor(out3.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 1.0
	}
	
	grad3 := flatten.Backward(gradOutput)
	grad2 := pool.Backward(grad3)
	gradInputAnalytical := conv.Backward(grad2)

	// Numerical gradient
	epsilon := 1e-5
	gradInputNumerical := utils.NewTensor(input.Shape...)
	
	for i := 0; i < len(input.Data); i++ {
		origVal := input.Data[i]
		
		input.Data[i] = origVal + epsilon
		lossPlus := computeLoss(input)
		
		input.Data[i] = origVal - epsilon
		lossMinus := computeLoss(input)
		
		input.Data[i] = origVal
		
		gradInputNumerical.Data[i] = (lossPlus - lossMinus) / (2 * epsilon)
	}

	// Compare gradients
	maxError := 0.0
	for i := range gradInputAnalytical.Data {
		error := math.Abs(gradInputAnalytical.Data[i] - gradInputNumerical.Data[i])
		if error > maxError {
			maxError = error
		}
	}
	
	// Allow some tolerance due to discrete nature of max pooling
	if maxError > 0.01 {
		t.Errorf("Gradient mismatch: max error = %f", maxError)
	}
}

// TestCNNIntegrationBatchProcessing tests that batch processing works correctly
func TestCNNIntegrationBatchProcessing(t *testing.T) {
	// Test with different batch sizes
	batchSizes := []int{1, 2, 8, 16}
	
	for _, batchSize := range batchSizes {
		input := utils.NewTensor(batchSize, 3, 16, 16)
		for i := range input.Data {
			input.Data[i] = float64(i%50) * 0.01
		}

		conv := NewConv2D(3, 8, []int{3, 3}, 1, "valid", true)
		pool := NewMaxPool2D([]int{2, 2}, 2, "valid")
		flatten := NewFlatten()

		// Forward pass
		out1 := conv.Forward(input, true)
		out2 := pool.Forward(out1, true)
		out3 := flatten.Forward(out2, true)

		// Check batch dimension preserved
		if out3.Shape[0] != batchSize {
			t.Errorf("Batch size %d: output batch size is %d", batchSize, out3.Shape[0])
		}

		// Backward pass
		gradOutput := utils.NewTensor(out3.Shape...)
		for i := range gradOutput.Data {
			gradOutput.Data[i] = 0.01
		}

		grad3 := flatten.Backward(gradOutput)
		grad2 := pool.Backward(grad3)
		grad1 := conv.Backward(grad2)

		// Check gradient batch dimension
		if grad1.Shape[0] != batchSize {
			t.Errorf("Batch size %d: gradient batch size is %d", batchSize, grad1.Shape[0])
		}
	}
}

// TestCNNIntegrationPaddingModes tests different padding modes in CNN
func TestCNNIntegrationPaddingModes(t *testing.T) {
	input := utils.NewTensor(1, 1, 7, 7)
	for i := range input.Data {
		input.Data[i] = float64(i+1)
	}

	// Test with valid padding
	convValid := NewConv2D(1, 2, []int{3, 3}, 1, "valid", false)
	poolValid := NewMaxPool2D([]int{2, 2}, 1, "valid")
	
	out1 := convValid.Forward(input, false)
	// Valid padding: 7 - 3 + 1 = 5
	if out1.Shape[2] != 5 || out1.Shape[3] != 5 {
		t.Errorf("Valid conv output shape error: got %v", out1.Shape)
	}
	
	out2 := poolValid.Forward(out1, false)
	// Valid pooling: 5 - 2 + 1 = 4
	if out2.Shape[2] != 4 || out2.Shape[3] != 4 {
		t.Errorf("Valid pool output shape error: got %v", out2.Shape)
	}

	// Test with same padding
	convSame := NewConv2D(1, 2, []int{3, 3}, 1, "same", false)
	poolSame := NewMaxPool2D([]int{2, 2}, 1, "same")
	
	out3 := convSame.Forward(input, false)
	// Same padding: maintains size
	if out3.Shape[2] != 7 || out3.Shape[3] != 7 {
		t.Errorf("Same conv output shape error: got %v", out3.Shape)
	}
	
	out4 := poolSame.Forward(out3, false)
	// Same pooling with stride 1: maintains size with asymmetric padding
	if out4.Shape[2] != 7 || out4.Shape[3] != 7 {
		t.Errorf("Same pool output shape error: expected [1 2 7 7], got %v", out4.Shape)
	}
}

// TestCNNIntegrationStrides tests different stride configurations
func TestCNNIntegrationStrides(t *testing.T) {
	input := utils.NewTensor(2, 1, 16, 16)
	for i := range input.Data {
		input.Data[i] = float64(i%10) * 0.1
	}

	// Test stride 1
	conv1 := NewConv2D(1, 4, []int{3, 3}, 1, "valid", false)
	out1 := conv1.Forward(input, false)
	// Output: (16 - 3) / 1 + 1 = 14
	if out1.Shape[2] != 14 || out1.Shape[3] != 14 {
		t.Errorf("Stride 1 conv output shape error: got %v", out1.Shape)
	}

	// Test stride 2
	conv2 := NewConv2D(1, 4, []int{3, 3}, 2, "valid", false)
	out2 := conv2.Forward(input, false)
	// Output: (16 - 3) / 2 + 1 = 7
	if out2.Shape[2] != 7 || out2.Shape[3] != 7 {
		t.Errorf("Stride 2 conv output shape error: got %v", out2.Shape)
	}

	// Test stride 3
	conv3 := NewConv2D(1, 4, []int{3, 3}, 3, "valid", false)
	out3 := conv3.Forward(input, false)
	// Output: (16 - 3) / 3 + 1 = 5
	if out3.Shape[2] != 5 || out3.Shape[3] != 5 {
		t.Errorf("Stride 3 conv output shape error: got %v", out3.Shape)
	}
}

// TestCNNIntegrationMemory tests memory usage patterns
func TestCNNIntegrationMemory(t *testing.T) {
	// Create a reasonably large input
	input := utils.NewTensor(8, 3, 64, 64)
	
	conv1 := NewConv2D(3, 32, []int{3, 3}, 1, "same", true)
	pool1 := NewMaxPool2D([]int{2, 2}, 2, "valid")
	conv2 := NewConv2D(32, 64, []int{3, 3}, 1, "same", true)
	pool2 := NewMaxPool2D([]int{2, 2}, 2, "valid")
	flatten := NewFlatten()

	// Multiple forward-backward passes
	for i := 0; i < 3; i++ {
		// Forward
		out1 := conv1.Forward(input, true)
		out2 := pool1.Forward(out1, true)
		out3 := conv2.Forward(out2, true)
		out4 := pool2.Forward(out3, true)
		out5 := flatten.Forward(out4, true)

		// Backward
		grad := utils.NewTensor(out5.Shape...)
		for j := range grad.Data {
			grad.Data[j] = 0.001
		}
		
		grad = flatten.Backward(grad)
		grad = pool2.Backward(grad)
		grad = conv2.Backward(grad)
		grad = pool1.Backward(grad)
		_ = conv1.Backward(grad)

		// Update weights (simulating training)
		conv1.UpdateWeights(0.001)
		conv2.UpdateWeights(0.001)
	}

	// If we get here without panic or memory issues, test passes
	t.Log("Memory test completed successfully")
}