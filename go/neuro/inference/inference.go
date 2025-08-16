package inference

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	
	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/layers"
	"github.com/muchq/moonbase/go/neuro/network"
	"github.com/muchq/moonbase/go/neuro/utils"
)

type InferenceEngine struct {
	model *network.Model
	config *ModelConfig
}

type ModelConfig struct {
	Version     string         `json:"version"`
	ModelType   string         `json:"model_type"`
	InputShape  []int          `json:"input_shape"`
	OutputShape []int          `json:"output_shape"`
	Layers      []LayerConfig  `json:"layers"`
	Classes     []string       `json:"classes,omitempty"`
}

type LayerConfig struct {
	Type       string                 `json:"type"`
	Name       string                 `json:"name"`
	Params     map[string]interface{} `json:"params"`
}

type ModelWeights struct {
	Version string          `json:"version"`
	Weights [][]float64     `json:"weights"`
}

func NewInferenceEngine() *InferenceEngine {
	return &InferenceEngine{
		model: network.NewModel(),
	}
}

func (e *InferenceEngine) LoadModel(configPath, weightsPath string) error {
	if err := e.loadConfig(configPath); err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}
	
	if err := e.buildModel(); err != nil {
		return fmt.Errorf("failed to build model: %w", err)
	}
	
	if err := e.loadWeights(weightsPath); err != nil {
		return fmt.Errorf("failed to load weights: %w", err)
	}
	
	return nil
}

func (e *InferenceEngine) loadConfig(path string) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()
	
	data, err := io.ReadAll(file)
	if err != nil {
		return err
	}
	
	e.config = &ModelConfig{}
	if err := json.Unmarshal(data, e.config); err != nil {
		return err
	}
	
	return nil
}

func (e *InferenceEngine) buildModel() error {
	for _, layerConfig := range e.config.Layers {
		layer, err := e.createLayer(layerConfig)
		if err != nil {
			return fmt.Errorf("failed to create layer %s: %w", layerConfig.Name, err)
		}
		e.model.Add(layer)
	}
	return nil
}

func (e *InferenceEngine) createLayer(config LayerConfig) (layers.Layer, error) {
	switch config.Type {
	case "Dense":
		inputSize := int(config.Params["input_size"].(float64))
		outputSize := int(config.Params["output_size"].(float64))
		
		var activation activations.Activation
		if actName, ok := config.Params["activation"].(string); ok {
			activation = e.createActivation(actName)
		}
		
		return layers.NewDense(inputSize, outputSize, activation), nil
		
	case "Conv2D":
		inChannels := int(config.Params["in_channels"].(float64))
		outChannels := int(config.Params["out_channels"].(float64))
		
		// Handle kernel_size which could be an array or a single value
		var kernelSize []int
		if ks, ok := config.Params["kernel_size"].([]interface{}); ok {
			kernelSize = make([]int, len(ks))
			for i, v := range ks {
				kernelSize[i] = int(v.(float64))
			}
		} else if ks, ok := config.Params["kernel_size"].(float64); ok {
			kernelSize = []int{int(ks), int(ks)}
		}
		
		stride := int(config.Params["stride"].(float64))
		padding := config.Params["padding"].(string)
		useBias := config.Params["use_bias"].(bool)
		
		return layers.NewConv2D(inChannels, outChannels, kernelSize, stride, padding, useBias), nil
		
	case "MaxPool2D":
		// Handle pool_size which could be an array or a single value
		var poolSize []int
		if ps, ok := config.Params["pool_size"].([]interface{}); ok {
			poolSize = make([]int, len(ps))
			for i, v := range ps {
				poolSize[i] = int(v.(float64))
			}
		} else if ps, ok := config.Params["pool_size"].(float64); ok {
			poolSize = []int{int(ps), int(ps)}
		}
		
		stride := int(config.Params["stride"].(float64))
		padding := config.Params["padding"].(string)
		
		return layers.NewMaxPool2D(poolSize, stride, padding), nil
		
	case "Flatten":
		return layers.NewFlatten(), nil
		
	case "Dropout":
		rate := config.Params["rate"].(float64)
		return layers.NewDropout(rate), nil
		
	case "BatchNorm":
		size := int(config.Params["size"].(float64))
		return layers.NewBatchNorm(size), nil
		
	default:
		return nil, fmt.Errorf("unknown layer type: %s", config.Type)
	}
}

func (e *InferenceEngine) createActivation(name string) activations.Activation {
	switch name {
	case "ReLU":
		return activations.NewReLU()
	case "Sigmoid":
		return activations.NewSigmoid()
	case "Tanh":
		return activations.NewTanh()
	case "Softmax":
		return activations.NewSoftmax()
	default:
		return nil
	}
}

func (e *InferenceEngine) loadWeights(path string) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()
	
	data, err := io.ReadAll(file)
	if err != nil {
		return err
	}
	
	var weights ModelWeights
	if err := json.Unmarshal(data, &weights); err != nil {
		return err
	}
	
	e.model.SetParams(weights.Weights)
	return nil
}

func (e *InferenceEngine) Predict(input *utils.Tensor) (*utils.Tensor, error) {
	if !e.validateInputShape(input.Shape) {
		return nil, fmt.Errorf("invalid input shape: expected %v, got %v", 
			e.config.InputShape, input.Shape)
	}
	
	return e.model.Predict(input), nil
}

func (e *InferenceEngine) PredictBatch(inputs []*utils.Tensor) ([]*utils.Tensor, error) {
	results := make([]*utils.Tensor, len(inputs))
	
	for i, input := range inputs {
		result, err := e.Predict(input)
		if err != nil {
			return nil, fmt.Errorf("failed to predict batch item %d: %w", i, err)
		}
		results[i] = result
	}
	
	return results, nil
}

func (e *InferenceEngine) PredictClass(input *utils.Tensor) (int, float64, error) {
	output, err := e.Predict(input)
	if err != nil {
		return -1, 0, err
	}
	
	maxIdx := 0
	maxVal := output.Data[0]
	
	for i := 1; i < len(output.Data); i++ {
		if output.Data[i] > maxVal {
			maxVal = output.Data[i]
			maxIdx = i
		}
	}
	
	return maxIdx, maxVal, nil
}

func (e *InferenceEngine) PredictClassName(input *utils.Tensor) (string, float64, error) {
	if len(e.config.Classes) == 0 {
		return "", 0, fmt.Errorf("no class names defined in model config")
	}
	
	classIdx, confidence, err := e.PredictClass(input)
	if err != nil {
		return "", 0, err
	}
	
	if classIdx >= len(e.config.Classes) {
		return "", 0, fmt.Errorf("class index %d out of range", classIdx)
	}
	
	return e.config.Classes[classIdx], confidence, nil
}

func (e *InferenceEngine) PredictTopK(input *utils.Tensor, k int) ([]int, []float64, error) {
	output, err := e.Predict(input)
	if err != nil {
		return nil, nil, err
	}
	
	type prediction struct {
		idx   int
		value float64
	}
	
	preds := make([]prediction, len(output.Data))
	for i, val := range output.Data {
		preds[i] = prediction{idx: i, value: val}
	}
	
	for i := 0; i < k && i < len(preds); i++ {
		maxIdx := i
		for j := i + 1; j < len(preds); j++ {
			if preds[j].value > preds[maxIdx].value {
				maxIdx = j
			}
		}
		preds[i], preds[maxIdx] = preds[maxIdx], preds[i]
	}
	
	topK := k
	if topK > len(preds) {
		topK = len(preds)
	}
	indices := make([]int, topK)
	values := make([]float64, topK)
	
	for i := 0; i < topK; i++ {
		indices[i] = preds[i].idx
		values[i] = preds[i].value
	}
	
	return indices, values, nil
}

func (e *InferenceEngine) validateInputShape(shape []int) bool {
	if len(shape) != len(e.config.InputShape) {
		return false
	}
	
	// Skip batch dimension (index 0) in validation, only check feature dimensions
	for i := 1; i < len(shape); i++ {
		if shape[i] != e.config.InputShape[i] {
			return false
		}
	}
	
	return true
}

func (e *InferenceEngine) GetModelInfo() *ModelConfig {
	return e.config
}

func (e *InferenceEngine) Warmup(numIterations int) error {
	dummyInput := utils.RandomTensor(e.config.InputShape...)
	
	for i := 0; i < numIterations; i++ {
		_, err := e.Predict(dummyInput)
		if err != nil {
			return fmt.Errorf("warmup failed at iteration %d: %w", i, err)
		}
	}
	
	return nil
}