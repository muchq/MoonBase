package inference

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	
	"github.com/muchq/moonbase/domains/ai/libs/neuro/layers"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/network"
)

type ModelSerializer struct {
	model *network.Model
}

func NewModelSerializer(model *network.Model) *ModelSerializer {
	return &ModelSerializer{
		model: model,
	}
}

func (s *ModelSerializer) Save(dirPath string) error {
	if err := os.MkdirAll(dirPath, 0755); err != nil {
		return fmt.Errorf("failed to create directory: %w", err)
	}
	
	configPath := filepath.Join(dirPath, "model_config.json")
	weightsPath := filepath.Join(dirPath, "model_weights.json")
	
	if err := s.saveConfig(configPath); err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}
	
	if err := s.saveWeights(weightsPath); err != nil {
		return fmt.Errorf("failed to save weights: %w", err)
	}
	
	return nil
}

func (s *ModelSerializer) saveConfig(path string) error {
	config := s.extractConfig()
	
	data, err := json.MarshalIndent(config, "", "  ")
	if err != nil {
		return err
	}
	
	return os.WriteFile(path, data, 0644)
}

func (s *ModelSerializer) extractConfig() *ModelConfig {
	config := &ModelConfig{
		Version:   "1.0",
		ModelType: "feedforward",
		Layers:    []LayerConfig{},
	}
	
	modelLayers := s.getModelLayers()
	
	// Infer input and output shapes from first and last layers
	if len(modelLayers) > 0 {
		// Get input shape from first layer
		if firstLayer, ok := modelLayers[0].(*layers.Dense); ok {
			config.InputShape = []int{1, firstLayer.InputSize}
		}
		
		// Get output shape from last layer
		for i := len(modelLayers) - 1; i >= 0; i-- {
			if lastLayer, ok := modelLayers[i].(*layers.Dense); ok {
				config.OutputShape = []int{1, lastLayer.OutputSize}
				break
			}
		}
	}
	
	for _, layer := range modelLayers {
		layerConfig := s.extractLayerConfig(layer)
		if layerConfig != nil {
			config.Layers = append(config.Layers, *layerConfig)
		}
	}
	
	return config
}

func (s *ModelSerializer) getModelLayers() []layers.Layer {
	return s.model.GetLayers()
}

func (s *ModelSerializer) extractLayerConfig(layer layers.Layer) *LayerConfig {
	switch l := layer.(type) {
	case *layers.Dense:
		return &LayerConfig{
			Type: "Dense",
			Name: l.Name(),
			Params: map[string]interface{}{
				"input_size":  l.InputSize,
				"output_size": l.OutputSize,
				"activation":  s.getActivationName(l),
			},
		}
	case *layers.Conv2D:
		return &LayerConfig{
			Type: "Conv2D",
			Name: l.Name(),
			Params: map[string]interface{}{
				"in_channels":  l.InChannels,
				"out_channels": l.OutChannels,
				"kernel_size":  l.KernelSize,
				"stride":       l.Stride,
				"padding":      l.Padding,
				"use_bias":     l.UseBias,
			},
		}
	case *layers.MaxPool2D:
		return &LayerConfig{
			Type: "MaxPool2D",
			Name: l.Name(),
			Params: map[string]interface{}{
				"pool_size": l.PoolSize,
				"stride":    l.Stride,
				"padding":   l.Padding,
			},
		}
	case *layers.Flatten:
		return &LayerConfig{
			Type: "Flatten",
			Name: l.Name(),
			Params: map[string]interface{}{},
		}
	case *layers.Dropout:
		// Note: We can't access the private 'rate' field, so we parse it from the name
		// or set a default. In production, you'd want to export the rate field.
		return &LayerConfig{
			Type: "Dropout",
			Name: l.Name(),
			Params: map[string]interface{}{
				"rate": 0.0, // Dropout is disabled during inference anyway
			},
		}
	case *layers.BatchNorm:
		return &LayerConfig{
			Type: "BatchNorm",
			Name: l.Name(),
			Params: map[string]interface{}{
				"size": l.Size,
			},
		}
	default:
		return nil
	}
}

func (s *ModelSerializer) getActivationName(dense *layers.Dense) string {
	if dense.Activation == nil {
		return "None"
	}
	return dense.Activation.Name()
}

func (s *ModelSerializer) saveWeights(path string) error {
	weights := &ModelWeights{
		Version: "1.0",
		Weights: s.model.GetParams(),
	}
	
	data, err := json.MarshalIndent(weights, "", "  ")
	if err != nil {
		return err
	}
	
	return os.WriteFile(path, data, 0644)
}

func SaveModel(model *network.Model, dirPath string) error {
	serializer := NewModelSerializer(model)
	return serializer.Save(dirPath)
}

func LoadModelForInference(dirPath string) (*InferenceEngine, error) {
	engine := NewInferenceEngine()
	
	configPath := filepath.Join(dirPath, "model_config.json")
	weightsPath := filepath.Join(dirPath, "model_weights.json")
	
	if err := engine.LoadModel(configPath, weightsPath); err != nil {
		return nil, err
	}
	
	return engine, nil
}

type CheckpointManager struct {
	basePath   string
	maxToKeep  int
	checkpoints []string
}

func NewCheckpointManager(basePath string, maxToKeep int) *CheckpointManager {
	cm := &CheckpointManager{
		basePath:    basePath,
		maxToKeep:   maxToKeep,
		checkpoints: []string{},
	}
	
	// Scan for existing checkpoints
	cm.scanExistingCheckpoints()
	
	return cm
}

// scanExistingCheckpoints looks for existing checkpoint directories
func (c *CheckpointManager) scanExistingCheckpoints() {
	// Check if base path exists
	if _, err := os.Stat(c.basePath); os.IsNotExist(err) {
		return
	}
	
	// Read directory contents
	entries, err := os.ReadDir(c.basePath)
	if err != nil {
		return
	}
	
	// Collect checkpoint directories
	type checkpointInfo struct {
		path  string
		epoch int
		batch int
	}
	var checkpoints []checkpointInfo
	
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		
		// Check if it's a checkpoint directory (handles both epoch and batch checkpoints)
		var epoch, batch int
		checkpointPath := filepath.Join(c.basePath, entry.Name())
		
		// Try to parse epoch+batch checkpoint first
		if n, err := fmt.Sscanf(entry.Name(), "checkpoint_epoch_%d_batch_%d", &epoch, &batch); err == nil && n == 2 {
			// It's a mid-epoch checkpoint
		} else if n, err := fmt.Sscanf(entry.Name(), "checkpoint_epoch_%d", &epoch); err == nil && n == 1 {
			// It's an epoch-end checkpoint
			batch = -1
		} else {
			continue // Not a valid checkpoint directory
		}
		
		// Verify it has the expected files
		configPath := filepath.Join(checkpointPath, "model_config.json")
		weightsPath := filepath.Join(checkpointPath, "model_weights.json")
		
		if _, err := os.Stat(configPath); err == nil {
			if _, err := os.Stat(weightsPath); err == nil {
				checkpoints = append(checkpoints, checkpointInfo{
					path:  checkpointPath,
					epoch: epoch,
					batch: batch,
				})
			}
		}
	}
	
	// Sort by epoch number, then by batch number
	for i := 0; i < len(checkpoints)-1; i++ {
		for j := i + 1; j < len(checkpoints); j++ {
			if checkpoints[i].epoch > checkpoints[j].epoch ||
				(checkpoints[i].epoch == checkpoints[j].epoch && checkpoints[i].batch > checkpoints[j].batch) {
				checkpoints[i], checkpoints[j] = checkpoints[j], checkpoints[i]
			}
		}
	}
	
	// Store the sorted checkpoint paths
	for _, cp := range checkpoints {
		c.checkpoints = append(c.checkpoints, cp.path)
	}
	
	// Keep only the most recent checkpoints if we have too many
	if len(c.checkpoints) > c.maxToKeep {
		c.cleanupOldCheckpoints()
	}
}

func (c *CheckpointManager) SaveCheckpoint(model *network.Model, epoch int, loss float64) error {
	checkpointDir := filepath.Join(c.basePath, fmt.Sprintf("checkpoint_epoch_%d", epoch))
	
	if err := SaveModel(model, checkpointDir); err != nil {
		return fmt.Errorf("failed to save checkpoint: %w", err)
	}
	
	metadataPath := filepath.Join(checkpointDir, "metadata.json")
	metadata := map[string]interface{}{
		"epoch": epoch,
		"loss":  loss,
	}
	
	data, err := json.MarshalIndent(metadata, "", "  ")
	if err != nil {
		return err
	}
	
	if err := os.WriteFile(metadataPath, data, 0644); err != nil {
		return err
	}
	
	c.checkpoints = append(c.checkpoints, checkpointDir)
	
	if len(c.checkpoints) > c.maxToKeep {
		oldCheckpoint := c.checkpoints[0]
		c.checkpoints = c.checkpoints[1:]
		os.RemoveAll(oldCheckpoint)
	}
	
	return nil
}

// SaveCheckpointWithProgress saves a checkpoint with detailed training progress
func (c *CheckpointManager) SaveCheckpointWithProgress(model *network.Model, epoch, batch int, loss float64, permutation []int, randomSeed int64) error {
	// Create a unique checkpoint name that includes batch info for mid-epoch checkpoints
	var checkpointDir string
	if batch >= 0 {
		checkpointDir = filepath.Join(c.basePath, fmt.Sprintf("checkpoint_epoch_%d_batch_%d", epoch, batch))
	} else {
		checkpointDir = filepath.Join(c.basePath, fmt.Sprintf("checkpoint_epoch_%d", epoch))
	}
	
	if err := SaveModel(model, checkpointDir); err != nil {
		return fmt.Errorf("failed to save checkpoint: %w", err)
	}
	
	// Save extended metadata including training state
	metadataPath := filepath.Join(checkpointDir, "metadata.json")
	metadata := map[string]interface{}{
		"epoch":       epoch,
		"batch":       batch,
		"loss":        loss,
		"random_seed": randomSeed,
	}
	
	// Save permutation separately as it can be large
	if len(permutation) > 0 {
		permPath := filepath.Join(checkpointDir, "permutation.json")
		permData, err := json.Marshal(permutation)
		if err != nil {
			return fmt.Errorf("failed to marshal permutation: %w", err)
		}
		if err := os.WriteFile(permPath, permData, 0644); err != nil {
			return fmt.Errorf("failed to save permutation: %w", err)
		}
	}
	
	data, err := json.MarshalIndent(metadata, "", "  ")
	if err != nil {
		return err
	}
	
	if err := os.WriteFile(metadataPath, data, 0644); err != nil {
		return err
	}
	
	// Update checkpoint list
	c.checkpoints = append(c.checkpoints, checkpointDir)
	
	// Clean up old checkpoints, but keep the most recent epoch-end checkpoints
	if len(c.checkpoints) > c.maxToKeep {
		// Priority: keep epoch-end checkpoints over mid-epoch ones
		c.cleanupOldCheckpoints()
	}
	
	return nil
}

// cleanupOldCheckpoints removes old checkpoints, prioritizing epoch-end checkpoints
func (c *CheckpointManager) cleanupOldCheckpoints() {
	if len(c.checkpoints) <= c.maxToKeep {
		return
	}
	
	// Simple strategy: just keep the most recent N checkpoints
	toRemove := c.checkpoints[:len(c.checkpoints)-c.maxToKeep]
	c.checkpoints = c.checkpoints[len(c.checkpoints)-c.maxToKeep:]
	
	for _, oldCheckpoint := range toRemove {
		os.RemoveAll(oldCheckpoint)
	}
}

func (c *CheckpointManager) LoadLatestCheckpoint() (*network.Model, error) {
	if len(c.checkpoints) == 0 {
		return nil, fmt.Errorf("no checkpoints available")
	}
	
	latestCheckpoint := c.checkpoints[len(c.checkpoints)-1]
	engine, err := LoadModelForInference(latestCheckpoint)
	if err != nil {
		return nil, err
	}
	
	return engine.model, nil
}

func (c *CheckpointManager) LoadLatestCheckpointWithMetadata() (*network.Model, int, error) {
	if len(c.checkpoints) == 0 {
		return nil, 0, fmt.Errorf("no checkpoints available")
	}
	
	latestCheckpoint := c.checkpoints[len(c.checkpoints)-1]
	
	// Load the model
	engine, err := LoadModelForInference(latestCheckpoint)
	if err != nil {
		return nil, 0, err
	}
	
	// Load metadata to get the epoch
	metadataPath := filepath.Join(latestCheckpoint, "metadata.json")
	data, err := os.ReadFile(metadataPath)
	if err != nil {
		return nil, 0, fmt.Errorf("failed to read metadata: %w", err)
	}
	
	var metadata map[string]interface{}
	if err := json.Unmarshal(data, &metadata); err != nil {
		return nil, 0, fmt.Errorf("failed to parse metadata: %w", err)
	}
	
	epoch := int(metadata["epoch"].(float64))
	
	return engine.model, epoch, nil
}

// TrainingState holds the complete state needed to resume training
type TrainingState struct {
	Model       *network.Model
	Epoch       int
	Batch       int
	Loss        float64
	Permutation []int
	RandomSeed  int64
}

// LoadLatestCheckpointWithFullState loads the latest checkpoint with complete training state
func (c *CheckpointManager) LoadLatestCheckpointWithFullState() (*TrainingState, error) {
	if len(c.checkpoints) == 0 {
		return nil, fmt.Errorf("no checkpoints available")
	}
	
	latestCheckpoint := c.checkpoints[len(c.checkpoints)-1]
	
	// Load the model
	engine, err := LoadModelForInference(latestCheckpoint)
	if err != nil {
		return nil, err
	}
	
	// Load metadata
	metadataPath := filepath.Join(latestCheckpoint, "metadata.json")
	data, err := os.ReadFile(metadataPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read metadata: %w", err)
	}
	
	var metadata map[string]interface{}
	if err := json.Unmarshal(data, &metadata); err != nil {
		return nil, fmt.Errorf("failed to parse metadata: %w", err)
	}
	
	state := &TrainingState{
		Model: engine.model,
		Epoch: int(metadata["epoch"].(float64)),
		Loss:  metadata["loss"].(float64),
	}
	
	// Load batch if present (for mid-epoch checkpoints)
	if batch, ok := metadata["batch"]; ok {
		state.Batch = int(batch.(float64))
	} else {
		state.Batch = -1 // Indicates end of epoch
	}
	
	// Load random seed if present
	if seed, ok := metadata["random_seed"]; ok {
		state.RandomSeed = int64(seed.(float64))
	}
	
	// Load permutation if it exists
	permPath := filepath.Join(latestCheckpoint, "permutation.json")
	if permData, err := os.ReadFile(permPath); err == nil {
		var perm []int
		if err := json.Unmarshal(permData, &perm); err == nil {
			state.Permutation = perm
		}
	}
	
	return state, nil
}

func ExportToONNX(model *network.Model, outputPath string) error {
	return fmt.Errorf("ONNX export not yet implemented")
}

func ImportFromONNX(inputPath string) (*network.Model, error) {
	return nil, fmt.Errorf("ONNX import not yet implemented")
}