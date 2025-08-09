package inference

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	
	"github.com/MoonBase/go/neuro/layers"
	"github.com/MoonBase/go/neuro/network"
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
	return &CheckpointManager{
		basePath:    basePath,
		maxToKeep:   maxToKeep,
		checkpoints: []string{},
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

func ExportToONNX(model *network.Model, outputPath string) error {
	return fmt.Errorf("ONNX export not yet implemented")
}

func ImportFromONNX(inputPath string) (*network.Model, error) {
	return nil, fmt.Errorf("ONNX import not yet implemented")
}