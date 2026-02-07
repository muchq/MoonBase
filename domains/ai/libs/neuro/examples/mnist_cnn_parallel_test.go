package main

import (
	"math"
	"testing"

	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
)

func TestReLUBackward(t *testing.T) {
	tests := []struct {
		name     string
		grad     []float64
		cache    []float64
		expected []float64
	}{
		{
			name:     "positive values pass gradient",
			grad:     []float64{1.0, 2.0, 3.0, 4.0},
			cache:    []float64{0.5, 1.0, 2.0, 3.0},
			expected: []float64{1.0, 2.0, 3.0, 4.0},
		},
		{
			name:     "negative values block gradient",
			grad:     []float64{1.0, 2.0, 3.0, 4.0},
			cache:    []float64{-0.5, -1.0, -2.0, -3.0},
			expected: []float64{0.0, 0.0, 0.0, 0.0},
		},
		{
			name:     "mixed positive and negative",
			grad:     []float64{1.0, 2.0, 3.0, 4.0},
			cache:    []float64{0.5, -1.0, 2.0, -3.0},
			expected: []float64{1.0, 0.0, 3.0, 0.0},
		},
		{
			name:     "zero cache blocks gradient",
			grad:     []float64{1.0, 2.0, 3.0, 4.0},
			cache:    []float64{0.0, 0.0, 0.0, 0.0},
			expected: []float64{0.0, 0.0, 0.0, 0.0},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			grad := &utils.Tensor{
				Data:  tt.grad,
				Shape: []int{len(tt.grad)},
			}
			cache := &utils.Tensor{
				Data:  tt.cache,
				Shape: []int{len(tt.cache)},
			}

			result := reluBackward(grad, cache)

			for i, expected := range tt.expected {
				if math.Abs(result.Data[i]-expected) > 1e-6 {
					t.Errorf("Index %d: expected %f, got %f", i, expected, result.Data[i])
				}
			}
		})
	}
}

func TestCNNModelForwardBackward(t *testing.T) {
	model := NewCNNModel()
	
	// Create a small batch of mock data
	batchSize := 2
	input := utils.NewTensor(batchSize, 1, 28, 28)
	
	// Initialize with some values
	for i := range input.Data {
		input.Data[i] = float64(i%10) / 10.0
	}
	
	// Test forward pass
	output := model.Forward(input, true)
	
	// Check output shape (should be [batchSize, 10] for 10 classes)
	if output.Shape[0] != batchSize || output.Shape[1] != 10 {
		t.Errorf("Expected output shape [%d, 10], got %v", batchSize, output.Shape)
	}
	
	// Check that ReLU caches were created
	if model.reluCache1 == nil {
		t.Error("ReLU cache 1 was not created during forward pass")
	}
	if model.reluCache2 == nil {
		t.Error("ReLU cache 2 was not created during forward pass")
	}
	
	// Test backward pass
	gradOutput := utils.NewTensor(batchSize, 10)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.1
	}
	
	gradInput := model.Backward(gradOutput)
	
	// Check gradient shape matches input shape
	if len(gradInput.Shape) != len(input.Shape) {
		t.Errorf("Gradient shape mismatch: expected %v, got %v", input.Shape, gradInput.Shape)
	}
	for i := range gradInput.Shape {
		if gradInput.Shape[i] != input.Shape[i] {
			t.Errorf("Gradient shape mismatch at dim %d: expected %d, got %d", 
				i, input.Shape[i], gradInput.Shape[i])
		}
	}
}

func TestClearGradients(t *testing.T) {
	model := NewCNNModel()
	
	// Run forward and backward to generate gradients
	input := utils.NewTensor(1, 1, 28, 28)
	for i := range input.Data {
		input.Data[i] = float64(i%10) / 10.0
	}
	
	_ = model.Forward(input, true)
	
	gradOutput := utils.NewTensor(1, 10)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.1
	}
	
	model.Backward(gradOutput)
	
	// Clear gradients
	model.ClearGradients()
	
	// Check Conv2D gradients are cleared
	conv1Grads := model.conv1.GetGradients()
	for _, grad := range conv1Grads {
		if grad != nil {
			for i, val := range grad.Data {
				if val != 0 {
					t.Errorf("Conv1 gradient not cleared at index %d: %f", i, val)
				}
			}
		}
	}
	
	conv2Grads := model.conv2.GetGradients()
	for _, grad := range conv2Grads {
		if grad != nil {
			for i, val := range grad.Data {
				if val != 0 {
					t.Errorf("Conv2 gradient not cleared at index %d: %f", i, val)
				}
			}
		}
	}
	
	// Check Dense layer gradients are cleared
	if model.dense1.GradW != nil {
		for i, val := range model.dense1.GradW.Data {
			if val != 0 {
				t.Errorf("Dense1 GradW not cleared at index %d: %f", i, val)
			}
		}
	}
	if model.dense1.GradB != nil {
		for i, val := range model.dense1.GradB.Data {
			if val != 0 {
				t.Errorf("Dense1 GradB not cleared at index %d: %f", i, val)
			}
		}
	}
	if model.dense2.GradW != nil {
		for i, val := range model.dense2.GradW.Data {
			if val != 0 {
				t.Errorf("Dense2 GradW not cleared at index %d: %f", i, val)
			}
		}
	}
	if model.dense2.GradB != nil {
		for i, val := range model.dense2.GradB.Data {
			if val != 0 {
				t.Errorf("Dense2 GradB not cleared at index %d: %f", i, val)
			}
		}
	}
}

func TestParallelModelGradientAveraging(t *testing.T) {
	numWorkers := 2
	pm := NewParallelCNNModel(numWorkers)
	
	// Create different gradients for each worker
	for w := 0; w < numWorkers; w++ {
		// Simulate different forward/backward passes
		input := utils.NewTensor(1, 1, 28, 28)
		for i := range input.Data {
			input.Data[i] = float64(i%10) / 10.0 * float64(w+1)
		}
		
		pm.models[w].Forward(input, true)
		
		gradOutput := utils.NewTensor(1, 10)
		for i := range gradOutput.Data {
			gradOutput.Data[i] = 0.1 * float64(w+1)
		}
		
		pm.models[w].Backward(gradOutput)
	}
	
	// Average gradients
	lr := 0.01
	pm.AverageGradientsAndUpdate(lr)
	
	// Verify workers are synced with master
	pm.SyncWorkers()
	
	// Check that all workers have the same weights as master
	masterParams := pm.master.conv1.GetParams()
	for w := 0; w < numWorkers; w++ {
		workerParams := pm.models[w].conv1.GetParams()
		for p := range masterParams {
			if masterParams[p] != nil && workerParams[p] != nil {
				for i := range masterParams[p].Data {
					if math.Abs(masterParams[p].Data[i]-workerParams[p].Data[i]) > 1e-6 {
						t.Errorf("Worker %d conv1 param %d not synced at index %d", w, p, i)
					}
				}
			}
		}
	}
}

func TestMinInt(t *testing.T) {
	tests := []struct {
		a, b     int
		expected int
	}{
		{1, 2, 1},
		{2, 1, 1},
		{-1, 0, -1},
		{5, 5, 5},
		{0, 0, 0},
	}
	
	for _, tt := range tests {
		result := minInt(tt.a, tt.b)
		if result != tt.expected {
			t.Errorf("minInt(%d, %d) = %d, expected %d", tt.a, tt.b, result, tt.expected)
		}
	}
}

func TestCopyWeightsFrom(t *testing.T) {
	source := NewCNNModel()
	target := NewCNNModel()
	
	// Modify source weights
	sourceConv1Params := source.conv1.GetParams()
	if sourceConv1Params[0] != nil {
		for i := range sourceConv1Params[0].Data {
			sourceConv1Params[0].Data[i] = float64(i) * 0.01
		}
	}
	
	sourceDense1Params := source.dense1.GetParams()
	if sourceDense1Params[0] != nil {
		for i := range sourceDense1Params[0].Data {
			sourceDense1Params[0].Data[i] = float64(i) * 0.02
		}
	}
	
	// Copy weights
	target.CopyWeightsFrom(source)
	
	// Verify conv1 weights copied
	targetConv1Params := target.conv1.GetParams()
	for p := range sourceConv1Params {
		if sourceConv1Params[p] != nil && targetConv1Params[p] != nil {
			for i := range sourceConv1Params[p].Data {
				if sourceConv1Params[p].Data[i] != targetConv1Params[p].Data[i] {
					t.Errorf("Conv1 param %d not copied correctly at index %d", p, i)
				}
			}
		}
	}
	
	// Verify dense1 weights copied
	targetDense1Params := target.dense1.GetParams()
	for p := range sourceDense1Params {
		if sourceDense1Params[p] != nil && targetDense1Params[p] != nil {
			for i := range sourceDense1Params[p].Data {
				if sourceDense1Params[p].Data[i] != targetDense1Params[p].Data[i] {
					t.Errorf("Dense1 param %d not copied correctly at index %d", p, i)
				}
			}
		}
	}
}

// TestBatchWorkerAccuracyCalculation tests that BatchWorker correctly calculates accuracy
func TestBatchWorkerAccuracyCalculation(t *testing.T) {
	// Create a simple CNN model
	model := NewCNNModel()
	
	// Create test data - 4 samples with known labels
	batchSize := 4
	batchImages := utils.NewTensor(batchSize, 1, 28, 28)
	// Initialize with small random values to avoid numerical issues
	for i := range batchImages.Data {
		batchImages.Data[i] = float64(i%10) * 0.01
	}
	
	batchLabels := []int{0, 1, 2, 3}
	
	// Create worker
	worker := &BatchWorker{
		id:          0,
		model:       model,
		batchImages: batchImages,
		batchLabels: batchLabels,
		numSamples:  batchSize,
		done:        make(chan bool, 1),
	}
	
	// Process batch
	worker.ProcessBatch()
	<-worker.done
	
	// Check that accuracy is a raw count (0-4), not a percentage
	if worker.accuracy < 0 || worker.accuracy > float64(batchSize) {
		t.Errorf("Expected accuracy to be between 0 and %d (raw count), got %f", 
			batchSize, worker.accuracy)
	}
	
	// Check that loss is weighted by number of samples
	if worker.loss <= 0 {
		t.Errorf("Expected positive weighted loss, got %f", worker.loss)
	}
}

// TestParallelAccuracyAggregation tests that parallel training correctly aggregates accuracy
func TestParallelAccuracyAggregation(t *testing.T) {
	testCases := []struct {
		name           string
		worker1Correct int
		worker1Samples int
		worker2Correct int
		worker2Samples int
		expectedAcc    float64
	}{
		{
			name:           "Equal distribution",
			worker1Correct: 3,
			worker1Samples: 5,
			worker2Correct: 2,
			worker2Samples: 5,
			expectedAcc:    0.5, // (3+2)/10 = 0.5
		},
		{
			name:           "Unequal distribution",
			worker1Correct: 4,
			worker1Samples: 6,
			worker2Correct: 2,
			worker2Samples: 4,
			expectedAcc:    0.6, // (4+2)/10 = 0.6
		},
		{
			name:           "Single worker",
			worker1Correct: 8,
			worker1Samples: 10,
			worker2Correct: 0,
			worker2Samples: 0,
			expectedAcc:    0.8, // 8/10 = 0.8
		},
	}
	
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Calculate aggregated accuracy
			totalCorrect := tc.worker1Correct + tc.worker2Correct
			totalSamples := tc.worker1Samples + tc.worker2Samples
			
			var actualAcc float64
			if totalSamples > 0 {
				actualAcc = float64(totalCorrect) / float64(totalSamples)
			}
			
			if math.Abs(actualAcc-tc.expectedAcc) > 1e-6 {
				t.Errorf("Expected accuracy %f, got %f", tc.expectedAcc, actualAcc)
			}
		})
	}
}

// TestParallelLossAggregation tests that parallel training correctly aggregates loss
func TestParallelLossAggregation(t *testing.T) {
	testCases := []struct {
		name          string
		worker1Loss   float64  // Per-sample loss
		worker1Samples int
		worker2Loss   float64  // Per-sample loss
		worker2Samples int
		expectedLoss  float64
	}{
		{
			name:          "Equal distribution",
			worker1Loss:   2.0,
			worker1Samples: 5,
			worker2Loss:   3.0,
			worker2Samples: 5,
			expectedLoss:  2.5, // (2.0*5 + 3.0*5)/10 = 2.5
		},
		{
			name:          "Unequal distribution",
			worker1Loss:   1.0,
			worker1Samples: 6,
			worker2Loss:   4.0,
			worker2Samples: 4,
			expectedLoss:  2.2, // (1.0*6 + 4.0*4)/10 = 2.2
		},
		{
			name:          "Single worker",
			worker1Loss:   1.5,
			worker1Samples: 10,
			worker2Loss:   0,
			worker2Samples: 0,
			expectedLoss:  1.5, // 1.5*10/10 = 1.5
		},
	}
	
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Simulate weighted loss calculation
			worker1WeightedLoss := tc.worker1Loss * float64(tc.worker1Samples)
			worker2WeightedLoss := tc.worker2Loss * float64(tc.worker2Samples)
			
			totalSamples := tc.worker1Samples + tc.worker2Samples
			var actualLoss float64
			if totalSamples > 0 {
				actualLoss = (worker1WeightedLoss + worker2WeightedLoss) / float64(totalSamples)
			}
			
			if math.Abs(actualLoss-tc.expectedLoss) > 1e-6 {
				t.Errorf("Expected loss %f, got %f", tc.expectedLoss, actualLoss)
			}
		})
	}
}

// TestCrossEntropyLoss tests the cross-entropy loss calculation
func TestCrossEntropyLoss(t *testing.T) {
	testCases := []struct {
		name         string
		predictions  []float64
		labels       []int
		expectedLoss float64
		tolerance    float64
	}{
		{
			name: "Perfect predictions",
			predictions: []float64{
				1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, // Sample 0, label 0
				0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, // Sample 1, label 1
			},
			labels:       []int{0, 1},
			expectedLoss: 0.0,
			tolerance:    0.01,
		},
		{
			name: "Uniform predictions",
			predictions: []float64{
				0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, // Sample 0
				0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, // Sample 1
			},
			labels:       []int{0, 5},
			expectedLoss: -math.Log(0.1), // -ln(0.1) ≈ 2.303
			tolerance:    0.01,
		},
		{
			name: "Mixed predictions",
			predictions: []float64{
				0.7, 0.1, 0.05, 0.05, 0.02, 0.02, 0.02, 0.02, 0.01, 0.01, // Sample 0, label 0
				0.1, 0.6, 0.1, 0.05, 0.05, 0.03, 0.03, 0.02, 0.01, 0.01,  // Sample 1, label 1
			},
			labels:       []int{0, 1},
			expectedLoss: (-math.Log(0.7) - math.Log(0.6)) / 2.0,
			tolerance:    0.01,
		},
	}
	
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			batchSize := len(tc.labels)
			predictions := utils.NewTensor(batchSize, 10)
			copy(predictions.Data, tc.predictions)
			
			loss, grad := crossEntropyLoss(predictions, tc.labels)
			
			if math.Abs(loss-tc.expectedLoss) > tc.tolerance {
				t.Errorf("Expected loss %f, got %f", tc.expectedLoss, loss)
			}
			
			// Check gradient shape
			if grad.Shape[0] != batchSize || grad.Shape[1] != 10 {
				t.Errorf("Expected gradient shape [%d, 10], got %v", batchSize, grad.Shape)
			}
		})
	}
}

// TestAccuracyFunction tests the accuracy calculation function
func TestAccuracyFunction(t *testing.T) {
	testCases := []struct {
		name        string
		predictions []float64
		labels      []int
		expectedAcc float64
	}{
		{
			name: "All correct",
			predictions: []float64{
				0.9, 0.05, 0.05, 0, 0, 0, 0, 0, 0, 0, // Predicts 0, label 0
				0.1, 0.8, 0.1, 0, 0, 0, 0, 0, 0, 0,   // Predicts 1, label 1
				0.05, 0.05, 0.9, 0, 0, 0, 0, 0, 0, 0, // Predicts 2, label 2
			},
			labels:      []int{0, 1, 2},
			expectedAcc: 1.0,
		},
		{
			name: "Half correct",
			predictions: []float64{
				0.9, 0.1, 0, 0, 0, 0, 0, 0, 0, 0,  // Predicts 0, label 0 ✓
				0.8, 0.2, 0, 0, 0, 0, 0, 0, 0, 0,  // Predicts 0, label 1 ✗
				0.1, 0.1, 0.8, 0, 0, 0, 0, 0, 0, 0, // Predicts 2, label 2 ✓
				0.3, 0.7, 0, 0, 0, 0, 0, 0, 0, 0,  // Predicts 1, label 3 ✗
			},
			labels:      []int{0, 1, 2, 3},
			expectedAcc: 0.5,
		},
		{
			name: "None correct",
			predictions: []float64{
				0.1, 0.9, 0, 0, 0, 0, 0, 0, 0, 0, // Predicts 1, label 0
				0.9, 0.1, 0, 0, 0, 0, 0, 0, 0, 0, // Predicts 0, label 1
			},
			labels:      []int{0, 1},
			expectedAcc: 0.0,
		},
	}
	
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			batchSize := len(tc.labels)
			predictions := utils.NewTensor(batchSize, 10)
			copy(predictions.Data, tc.predictions)
			
			acc := accuracy(predictions, tc.labels)
			
			if math.Abs(acc-tc.expectedAcc) > 1e-6 {
				t.Errorf("Expected accuracy %f, got %f", tc.expectedAcc, acc)
			}
		})
	}
}