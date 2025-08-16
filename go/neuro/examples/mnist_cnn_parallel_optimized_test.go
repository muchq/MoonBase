package main

import (
	"math"
	"sync"
	"testing"
	"time"

	"github.com/muchq/moonbase/go/neuro/utils"
)

func TestTensorPool(t *testing.T) {
	pool := NewTensorPool()
	
	// Test getting tensors
	t1 := pool.Get(10, 10)
	if t1 == nil {
		t.Fatal("Failed to get tensor from pool")
	}
	if len(t1.Data) != 100 {
		t.Errorf("Expected tensor with 100 elements, got %d", len(t1.Data))
	}
	
	// Verify tensor is zeroed
	for i, v := range t1.Data {
		if v != 0 {
			t.Errorf("Tensor not zeroed at index %d: %f", i, v)
		}
	}
	
	// Put tensor back
	t1.Data[0] = 1.0 // Modify to test clearing
	pool.Put(t1)
	
	// Get another tensor of same shape - should reuse
	t2 := pool.Get(10, 10)
	if t2.Data[0] != 0 {
		t.Error("Reused tensor not cleared")
	}
	
	// Test concurrent access
	var wg sync.WaitGroup
	for i := 0; i < 10; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			tensor := pool.Get(5, 5, 5)
			time.Sleep(time.Millisecond)
			pool.Put(tensor)
		}()
	}
	wg.Wait()
}

func TestGradientClipping(t *testing.T) {
	tests := []struct {
		name        string
		gradValues  []float64
		maxNorm     float64
		expectClip  bool
	}{
		{
			name:       "no clipping needed",
			gradValues: []float64{0.1, 0.2, 0.1},
			maxNorm:    1.0,
			expectClip: false,
		},
		{
			name:       "clipping needed",
			gradValues: []float64{3.0, 4.0, 0.0}, // norm = 5
			maxNorm:    2.5,
			expectClip: true,
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Create gradient tensors
			grads := [][]*utils.Tensor{
				{&utils.Tensor{Data: tt.gradValues, Shape: []int{len(tt.gradValues)}}},
			}
			
			// Compute original norm
			origNorm := 0.0
			for _, v := range tt.gradValues {
				origNorm += v * v
			}
			origNorm = math.Sqrt(origNorm)
			
			// Clip gradients
			finalNorm := clipGradients(grads, tt.maxNorm)
			
			// Check if clipping occurred as expected
			if tt.expectClip {
				if finalNorm != tt.maxNorm {
					t.Errorf("Expected norm to be clipped to %f, got %f", tt.maxNorm, finalNorm)
				}
				
				// Verify gradients were scaled  
				// Recompute actual norm after clipping
				actualNorm := 0.0
				for _, v := range grads[0][0].Data {
					actualNorm += v * v
				}
				actualNorm = math.Sqrt(actualNorm)
				
				if math.Abs(actualNorm-tt.maxNorm) > 1e-6 {
					t.Errorf("Gradient norm after clipping is %f, expected %f", actualNorm, tt.maxNorm)
				}
			} else {
				if finalNorm != origNorm {
					t.Errorf("Expected no clipping, but norm changed from %f to %f", origNorm, finalNorm)
				}
			}
		})
	}
}

func TestGradientStats(t *testing.T) {
	tensor := &utils.Tensor{
		Data:  []float64{1.0, -2.0, 3.0, -4.0, 5.0},
		Shape: []int{5},
	}
	
	stats := computeGradientStats(tensor)
	
	if stats.min != -4.0 {
		t.Errorf("Expected min -4.0, got %f", stats.min)
	}
	if stats.max != 5.0 {
		t.Errorf("Expected max 5.0, got %f", stats.max)
	}
	if math.Abs(stats.mean-0.6) > 1e-6 {
		t.Errorf("Expected mean 0.6, got %f", stats.mean)
	}
	
	// Check norm (sqrt(1+4+9+16+25) = sqrt(55))
	expectedNorm := math.Sqrt(55.0)
	if math.Abs(stats.norm-expectedNorm) > 1e-6 {
		t.Errorf("Expected norm %f, got %f", expectedNorm, stats.norm)
	}
}

func TestParallelTrainerSynchronization(t *testing.T) {
	trainer := NewParallelTrainer(2, 5.0)
	defer trainer.Stop()
	
	// Create test batch
	batchSize := 4
	images := utils.NewTensor(batchSize, 1, 28, 28)
	labels := []int{0, 1, 2, 3}
	
	// Initialize with some values
	for i := range images.Data {
		images.Data[i] = float64(i%10) / 10.0
	}
	
	// Process batch
	trainer.ProcessBatch(images, labels, true)
	
	// Collect result - should not hang
	done := make(chan bool)
	go func() {
		trainer.CollectResults(1, 0.01)
		done <- true
	}()
	
	select {
	case <-done:
		// Success - no deadlock
	case <-time.After(2 * time.Second):
		t.Fatal("Deadlock detected in result collection")
	}
}

func TestPipelineProcessing(t *testing.T) {
	trainer := NewParallelTrainer(4, 5.0)
	defer trainer.Stop()
	
	// Submit multiple batches
	numBatches := 8
	batchSize := 2
	
	for i := 0; i < numBatches; i++ {
		images := trainer.tensorPool.Get(batchSize, 1, 28, 28)
		labels := make([]int, batchSize)
		
		// Initialize with different values for each batch
		for j := range images.Data {
			images.Data[j] = float64(i*j%10) / 10.0
		}
		for j := range labels {
			labels[j] = i % 10
		}
		
		trainer.ProcessBatch(images, labels, true)
	}
	
	// Collect all results
	totalLoss := 0.0
	totalAcc := 0.0
	
	for i := 0; i < numBatches; i++ {
		loss, acc, _ := trainer.CollectResults(1, 0.01)
		totalLoss += loss
		totalAcc += acc
	}
	
	// Verify we got results from all batches
	if totalLoss == 0 {
		t.Error("No loss computed from batches")
	}
}

func TestMemoryEfficiency(t *testing.T) {
	pool := NewTensorPool()
	
	// Track tensor allocations
	allocations := make(map[*utils.Tensor]bool)
	
	// Get and return tensors multiple times
	for i := 0; i < 100; i++ {
		tensor := pool.Get(10, 10)
		allocations[tensor] = true
		pool.Put(tensor)
	}
	
	// Should have reused tensors, not created 100 unique ones
	if len(allocations) > 10 {
		t.Errorf("Too many unique tensors allocated: %d (expected reuse)", len(allocations))
	}
}

func TestOptimizedModelGradientAccumulation(t *testing.T) {
	model := NewOptimizedCNNModel(false)
	
	// Run forward and backward
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
	
	// Get gradients
	grads := model.GetGradients()
	
	// Verify gradients exist
	hasNonZeroGrad := false
	for _, layerGrads := range grads {
		for _, grad := range layerGrads {
			if grad != nil {
				for _, v := range grad.Data {
					if v != 0 {
						hasNonZeroGrad = true
						break
					}
				}
			}
		}
	}
	
	if !hasNonZeroGrad {
		t.Error("No gradients were computed")
	}
	
	// Apply gradients
	model.ApplyGradients(grads, 0.01)
	
	// Verify weights were updated
	params := model.conv1.GetParams()
	if params == nil || len(params) == 0 {
		t.Error("No parameters found after gradient application")
	}
}

func BenchmarkTensorPool(b *testing.B) {
	pool := NewTensorPool()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		tensor := pool.Get(100, 100)
		pool.Put(tensor)
	}
}

func BenchmarkGradientClipping(b *testing.B) {
	// Create test gradients
	grads := make([][]*utils.Tensor, 4)
	for i := range grads {
		grads[i] = make([]*utils.Tensor, 2)
		grads[i][0] = utils.NewTensor(100, 100)
		grads[i][1] = utils.NewTensor(100)
		
		// Initialize with random values
		for j := range grads[i][0].Data {
			grads[i][0].Data[j] = float64(j%10) / 10.0
		}
		for j := range grads[i][1].Data {
			grads[i][1].Data[j] = float64(j%5) / 10.0
		}
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		clipGradients(grads, 5.0)
	}
}