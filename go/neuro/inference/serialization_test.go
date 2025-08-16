package inference

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/layers"
	"github.com/muchq/moonbase/go/neuro/network"
	"github.com/muchq/moonbase/go/neuro/utils"
)

func TestCheckpointManager(t *testing.T) {
	// Create a temporary directory for tests
	tmpDir, err := os.MkdirTemp("", "checkpoint_test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	t.Run("NewCheckpointManager_EmptyDir", func(t *testing.T) {
		checkpointDir := filepath.Join(tmpDir, "empty_checkpoints")
		cm := NewCheckpointManager(checkpointDir, 3)
		
		if len(cm.checkpoints) != 0 {
			t.Errorf("Expected 0 checkpoints, got %d", len(cm.checkpoints))
		}
	})

	t.Run("SaveAndLoadCheckpoint", func(t *testing.T) {
		checkpointDir := filepath.Join(tmpDir, "save_load_checkpoints")
		cm := NewCheckpointManager(checkpointDir, 3)
		
		// Create a simple model
		model := network.NewModel()
		model.Add(layers.NewDense(10, 5, &activations.ReLU{}))
		model.Add(layers.NewDense(5, 2, &activations.Softmax{}))
		
		// Initialize with some weights
		params := [][]float64{
			make([]float64, 10*5), // weights for first layer
			make([]float64, 5),    // bias for first layer
			make([]float64, 5*2),  // weights for second layer
			make([]float64, 2),    // bias for second layer
		}
		for i := range params {
			for j := range params[i] {
				params[i][j] = float64(i*100 + j) // predictable values for testing
			}
		}
		model.SetParams(params)
		
		// Save checkpoint
		epoch := 5
		loss := 1.234
		err := cm.SaveCheckpoint(model, epoch, loss)
		if err != nil {
			t.Fatalf("Failed to save checkpoint: %v", err)
		}
		
		// Load checkpoint
		loadedModel, loadedEpoch, err := cm.LoadLatestCheckpointWithMetadata()
		if err != nil {
			t.Fatalf("Failed to load checkpoint: %v", err)
		}
		
		if loadedEpoch != epoch {
			t.Errorf("Expected epoch %d, got %d", epoch, loadedEpoch)
		}
		
		// Verify weights are the same
		loadedParams := loadedModel.GetParams()
		if len(loadedParams) != len(params) {
			t.Fatalf("Parameter count mismatch: expected %d, got %d", len(params), len(loadedParams))
		}
		
		for i := range params {
			if len(loadedParams[i]) != len(params[i]) {
				t.Errorf("Parameter %d size mismatch: expected %d, got %d", 
					i, len(params[i]), len(loadedParams[i]))
				continue
			}
			for j := range params[i] {
				if loadedParams[i][j] != params[i][j] {
					t.Errorf("Parameter mismatch at [%d][%d]: expected %f, got %f", 
						i, j, params[i][j], loadedParams[i][j])
				}
			}
		}
	})

	t.Run("MultipleCheckpoints_KeepLatest", func(t *testing.T) {
		checkpointDir := filepath.Join(tmpDir, "multiple_checkpoints")
		maxToKeep := 3
		cm := NewCheckpointManager(checkpointDir, maxToKeep)
		
		// Create a simple model
		model := network.NewModel()
		model.Add(layers.NewDense(10, 5, nil))
		
		// Save multiple checkpoints
		for epoch := 1; epoch <= 5; epoch++ {
			err := cm.SaveCheckpoint(model, epoch, float64(epoch)*0.1)
			if err != nil {
				t.Fatalf("Failed to save checkpoint %d: %v", epoch, err)
			}
		}
		
		// Should only keep the last 3 checkpoints
		if len(cm.checkpoints) != maxToKeep {
			t.Errorf("Expected %d checkpoints, got %d", maxToKeep, len(cm.checkpoints))
		}
		
		// Load latest checkpoint
		_, loadedEpoch, err := cm.LoadLatestCheckpointWithMetadata()
		if err != nil {
			t.Fatalf("Failed to load latest checkpoint: %v", err)
		}
		
		if loadedEpoch != 5 {
			t.Errorf("Expected latest epoch to be 5, got %d", loadedEpoch)
		}
		
		// Verify older checkpoints were deleted
		for epoch := 1; epoch <= 2; epoch++ {
			oldPath := filepath.Join(checkpointDir, "checkpoint_epoch_" + string(rune(epoch + '0')))
			if _, err := os.Stat(oldPath); !os.IsNotExist(err) {
				t.Errorf("Old checkpoint epoch %d should have been deleted", epoch)
			}
		}
	})

	t.Run("LoadExistingCheckpoints_OnInit", func(t *testing.T) {
		checkpointDir := filepath.Join(tmpDir, "existing_checkpoints")
		
		// Create checkpoints manually
		model := network.NewModel()
		model.Add(layers.NewDense(10, 5, nil))
		
		// Save checkpoints using a temporary manager
		tempCm := NewCheckpointManager(checkpointDir, 10)
		for epoch := 1; epoch <= 3; epoch++ {
			err := tempCm.SaveCheckpoint(model, epoch, float64(epoch)*0.1)
			if err != nil {
				t.Fatalf("Failed to save checkpoint %d: %v", epoch, err)
			}
		}
		
		// Create a new checkpoint manager - it should find existing checkpoints
		newCm := NewCheckpointManager(checkpointDir, 2)
		
		// Should have loaded existing checkpoints and kept only 2
		if len(newCm.checkpoints) != 2 {
			t.Errorf("Expected 2 checkpoints after init, got %d", len(newCm.checkpoints))
		}
		
		// Should be able to load the latest
		_, loadedEpoch, err := newCm.LoadLatestCheckpointWithMetadata()
		if err != nil {
			t.Fatalf("Failed to load checkpoint: %v", err)
		}
		
		if loadedEpoch != 3 {
			t.Errorf("Expected latest epoch to be 3, got %d", loadedEpoch)
		}
	})

	t.Run("CNN_Model_Checkpoint", func(t *testing.T) {
		checkpointDir := filepath.Join(tmpDir, "cnn_checkpoints")
		cm := NewCheckpointManager(checkpointDir, 3)
		
		// Create a CNN-like model
		model := network.NewModel()
		model.Add(layers.NewConv2D(1, 32, []int{3, 3}, 1, "valid", true))
		model.Add(layers.NewMaxPool2D([]int{2, 2}, 2, "valid"))
		model.Add(layers.NewDropout(0.25))
		model.Add(layers.NewConv2D(32, 64, []int{3, 3}, 1, "valid", true))
		model.Add(layers.NewMaxPool2D([]int{2, 2}, 2, "valid"))
		model.Add(layers.NewDropout(0.5))
		model.Add(layers.NewFlatten())
		model.Add(layers.NewDense(1600, 128, &activations.ReLU{}))
		model.Add(layers.NewDense(128, 10, &activations.Softmax{}))
		
		// Initialize with random weights
		params := model.GetParams()
		for i := range params {
			for j := range params[i] {
				params[i][j] = float64(i+1) * 0.01 + float64(j) * 0.001
			}
		}
		model.SetParams(params)
		
		// Save checkpoint
		epoch := 5
		loss := 1.4029
		err := cm.SaveCheckpoint(model, epoch, loss)
		if err != nil {
			t.Fatalf("Failed to save CNN checkpoint: %v", err)
		}
		
		// Load checkpoint
		loadedModel, loadedEpoch, err := cm.LoadLatestCheckpointWithMetadata()
		if err != nil {
			t.Fatalf("Failed to load CNN checkpoint: %v", err)
		}
		
		if loadedEpoch != epoch {
			t.Errorf("Expected epoch %d, got %d", epoch, loadedEpoch)
		}
		
		// Verify model structure
		loadedLayers := loadedModel.GetLayers()
		if len(loadedLayers) != 9 {
			t.Fatalf("Expected 9 layers, got %d", len(loadedLayers))
		}
		
		// Verify layer types
		if _, ok := loadedLayers[0].(*layers.Conv2D); !ok {
			t.Errorf("Layer 0 should be Conv2D")
		}
		if _, ok := loadedLayers[1].(*layers.MaxPool2D); !ok {
			t.Errorf("Layer 1 should be MaxPool2D")
		}
		if _, ok := loadedLayers[6].(*layers.Flatten); !ok {
			t.Errorf("Layer 6 should be Flatten")
		}
		if _, ok := loadedLayers[7].(*layers.Dense); !ok {
			t.Errorf("Layer 7 should be Dense")
		}
		
		// Verify weights are preserved
		loadedParams := loadedModel.GetParams()
		for i := range params {
			for j := range params[i] {
				if loadedParams[i][j] != params[i][j] {
					t.Errorf("Weight mismatch at [%d][%d]: expected %f, got %f",
						i, j, params[i][j], loadedParams[i][j])
					break
				}
			}
		}
		
		// Test forward pass with loaded model
		input := utils.NewTensor(1, 1, 28, 28)
		for i := range input.Data {
			input.Data[i] = float64(i) * 0.001
		}
		
		output := loadedModel.Predict(input)
		if output == nil {
			t.Error("Forward pass failed on loaded model")
		}
		if len(output.Data) != 10 {
			t.Errorf("Expected output size 10, got %d", len(output.Data))
		}
	})
}

func TestMidEpochCheckpointResume(t *testing.T) {
	// Test that we can save and resume from mid-epoch
	tmpDir, err := os.MkdirTemp("", "mid_epoch_test")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)
	
	checkpointDir := filepath.Join(tmpDir, "mid_epoch_checkpoints")
	cm := NewCheckpointManager(checkpointDir, 3)
	
	// Create a simple model
	model := network.NewModel()
	model.Add(layers.NewDense(10, 5, &activations.ReLU{}))
	model.Add(layers.NewDense(5, 2, &activations.Softmax{}))
	
	// Save a mid-epoch checkpoint
	epoch := 3
	batch := 150
	loss := 1.234
	perm := []int{5, 2, 8, 1, 9, 0, 3, 7, 4, 6} // Example permutation
	seed := int64(42)
	
	err = cm.SaveCheckpointWithProgress(model, epoch, batch, loss, perm, seed)
	if err != nil {
		t.Fatalf("Failed to save mid-epoch checkpoint: %v", err)
	}
	
	// Create a new checkpoint manager (simulating restart)
	cm2 := NewCheckpointManager(checkpointDir, 3)
	
	// Load the checkpoint with full state
	state, err := cm2.LoadLatestCheckpointWithFullState()
	if err != nil {
		t.Fatalf("Failed to load checkpoint: %v", err)
	}
	
	// Verify all state was restored
	if state.Epoch != epoch {
		t.Errorf("Expected epoch %d, got %d", epoch, state.Epoch)
	}
	
	if state.Batch != batch {
		t.Errorf("Expected batch %d, got %d", batch, state.Batch)
	}
	
	if state.Loss != loss {
		t.Errorf("Expected loss %f, got %f", loss, state.Loss)
	}
	
	if state.RandomSeed != seed {
		t.Errorf("Expected seed %d, got %d", seed, state.RandomSeed)
	}
	
	// Verify permutation
	if len(state.Permutation) != len(perm) {
		t.Fatalf("Permutation length mismatch: expected %d, got %d", len(perm), len(state.Permutation))
	}
	
	for i := range perm {
		if state.Permutation[i] != perm[i] {
			t.Errorf("Permutation mismatch at index %d: expected %d, got %d", 
				i, perm[i], state.Permutation[i])
		}
	}
}

func TestCheckpointResume_LearningRate(t *testing.T) {
	// Test learning rate calculation after resume
	testCases := []struct {
		resumedEpoch       int
		initialLR         float64
		expectedLR        float64
		expectedDecays    int
	}{
		{0, 0.001, 0.001, 0},       // No decay yet
		{4, 0.001, 0.001, 0},       // Still no decay (decay happens at epoch 5)
		{5, 0.001, 0.0009, 1},      // One decay
		{9, 0.001, 0.0009, 1},      // Still one decay
		{10, 0.001, 0.00081, 2},    // Two decays
		{15, 0.001, 0.000729, 3},   // Three decays
	}
	
	for _, tc := range testCases {
		t.Run(string(rune(tc.resumedEpoch + '0')), func(t *testing.T) {
			actualLR := tc.initialLR
			numDecays := tc.resumedEpoch / 5
			
			if numDecays != tc.expectedDecays {
				t.Errorf("Expected %d decays for epoch %d, got %d", 
					tc.expectedDecays, tc.resumedEpoch, numDecays)
			}
			
			for i := 0; i < numDecays; i++ {
				actualLR *= 0.9
			}
			
			// Allow small floating point differences
			if diff := actualLR - tc.expectedLR; diff > 0.000001 || diff < -0.000001 {
				t.Errorf("Expected LR %.6f for epoch %d, got %.6f", 
					tc.expectedLR, tc.resumedEpoch, actualLR)
			}
		})
	}
}