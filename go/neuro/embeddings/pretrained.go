package embeddings

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"unsafe"

	"github.com/muchq/moonbase/go/neuro/utils"
)

type ModelFormat string

const (
	ONNXFormat        ModelFormat = "onnx"
	SafeTensorsFormat ModelFormat = "safetensors"
	CustomFormat      ModelFormat = "custom"
)

type PretrainedModel struct {
	Name         string
	Architecture string
	EmbeddingDim int
	VocabSize    int
	NumLayers    int
	NumHeads     int
	HiddenDim    int
	MaxSeqLength int
	Weights      map[string]*utils.Tensor
}

type ModelLoader interface {
	Load(path string) (*PretrainedModel, error)
	Save(model *PretrainedModel, path string) error
	Format() ModelFormat
}

type CustomModelLoader struct{}

func NewCustomModelLoader() *CustomModelLoader {
	return &CustomModelLoader{}
}

func (cml *CustomModelLoader) Load(path string) (*PretrainedModel, error) {
	configPath := filepath.Join(path, "config.json")
	weightsPath := filepath.Join(path, "weights.bin")

	config, err := cml.loadConfig(configPath)
	if err != nil {
		return nil, fmt.Errorf("failed to load config: %w", err)
	}

	weights, err := cml.loadWeights(weightsPath, config)
	if err != nil {
		return nil, fmt.Errorf("failed to load weights: %w", err)
	}

	model := &PretrainedModel{
		Name:         config.Name,
		Architecture: config.Architecture,
		EmbeddingDim: config.EmbeddingDim,
		VocabSize:    config.VocabSize,
		NumLayers:    config.NumLayers,
		NumHeads:     config.NumHeads,
		HiddenDim:    config.HiddenDim,
		MaxSeqLength: config.MaxSeqLength,
		Weights:      weights,
	}

	return model, nil
}

func (cml *CustomModelLoader) Save(model *PretrainedModel, path string) error {
	if err := os.MkdirAll(path, 0755); err != nil {
		return fmt.Errorf("failed to create directory: %w", err)
	}

	configPath := filepath.Join(path, "config.json")
	if err := cml.saveConfig(model, configPath); err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}

	weightsPath := filepath.Join(path, "weights.bin")
	if err := cml.saveWeights(model.Weights, weightsPath); err != nil {
		return fmt.Errorf("failed to save weights: %w", err)
	}

	return nil
}

func (cml *CustomModelLoader) Format() ModelFormat {
	return CustomFormat
}

type ModelConfig struct {
	Name         string `json:"name"`
	Architecture string `json:"architecture"`
	EmbeddingDim int    `json:"embedding_dim"`
	VocabSize    int    `json:"vocab_size"`
	NumLayers    int    `json:"num_layers"`
	NumHeads     int    `json:"num_heads"`
	HiddenDim    int    `json:"hidden_dim"`
	MaxSeqLength int    `json:"max_seq_length"`
}

func (cml *CustomModelLoader) loadConfig(path string) (*ModelConfig, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	var config ModelConfig
	decoder := json.NewDecoder(file)
	if err := decoder.Decode(&config); err != nil {
		return nil, err
	}

	return &config, nil
}

func (cml *CustomModelLoader) saveConfig(model *PretrainedModel, path string) error {
	config := ModelConfig{
		Name:         model.Name,
		Architecture: model.Architecture,
		EmbeddingDim: model.EmbeddingDim,
		VocabSize:    model.VocabSize,
		NumLayers:    model.NumLayers,
		NumHeads:     model.NumHeads,
		HiddenDim:    model.HiddenDim,
		MaxSeqLength: model.MaxSeqLength,
	}

	file, err := os.Create(path)
	if err != nil {
		return err
	}
	defer file.Close()

	encoder := json.NewEncoder(file)
	encoder.SetIndent("", "  ")
	return encoder.Encode(config)
}

func (cml *CustomModelLoader) loadWeights(path string, config *ModelConfig) (map[string]*utils.Tensor, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	weights := make(map[string]*utils.Tensor)

	for {
		var nameLen int32
		if err := readBinary(file, &nameLen); err != nil {
			if err == io.EOF {
				break
			}
			return nil, err
		}

		nameBytes := make([]byte, nameLen)
		if _, err := io.ReadFull(file, nameBytes); err != nil {
			return nil, err
		}
		name := string(nameBytes)

		var numDims int32
		if err := readBinary(file, &numDims); err != nil {
			return nil, err
		}

		shape := make([]int, numDims)
		for i := 0; i < int(numDims); i++ {
			var dim int32
			if err := readBinary(file, &dim); err != nil {
				return nil, err
			}
			shape[i] = int(dim)
		}

		numElements := 1
		for _, dim := range shape {
			numElements *= dim
		}

		data := make([]float64, numElements)
		for i := 0; i < numElements; i++ {
			var val float32
			if err := readBinary(file, &val); err != nil {
				return nil, err
			}
			data[i] = float64(val)
		}

		weights[name] = utils.NewTensorFromData(data, shape...)
	}

	return weights, nil
}

func (cml *CustomModelLoader) saveWeights(weights map[string]*utils.Tensor, path string) error {
	file, err := os.Create(path)
	if err != nil {
		return err
	}
	defer file.Close()

	for name, tensor := range weights {
		nameBytes := []byte(name)
		nameLen := int32(len(nameBytes))

		if err := writeBinary(file, nameLen); err != nil {
			return err
		}

		if _, err := file.Write(nameBytes); err != nil {
			return err
		}

		numDims := int32(len(tensor.Shape))
		if err := writeBinary(file, numDims); err != nil {
			return err
		}

		for _, dim := range tensor.Shape {
			if err := writeBinary(file, int32(dim)); err != nil {
				return err
			}
		}

		for _, val := range tensor.Data {
			if err := writeBinary(file, float32(val)); err != nil {
				return err
			}
		}
	}

	return nil
}

func readBinary(r io.Reader, data interface{}) error {
	switch v := data.(type) {
	case *int32:
		bytes := make([]byte, 4)
		if _, err := io.ReadFull(r, bytes); err != nil {
			return err
		}
		*v = int32(bytes[0]) | int32(bytes[1])<<8 | int32(bytes[2])<<16 | int32(bytes[3])<<24
	case *float32:
		bytes := make([]byte, 4)
		if _, err := io.ReadFull(r, bytes); err != nil {
			return err
		}
		bits := uint32(bytes[0]) | uint32(bytes[1])<<8 | uint32(bytes[2])<<16 | uint32(bytes[3])<<24
		*v = float32frombits(bits)
	default:
		return fmt.Errorf("unsupported type: %T", v)
	}
	return nil
}

func writeBinary(w io.Writer, data interface{}) error {
	switch v := data.(type) {
	case int32:
		bytes := []byte{
			byte(v),
			byte(v >> 8),
			byte(v >> 16),
			byte(v >> 24),
		}
		_, err := w.Write(bytes)
		return err
	case float32:
		bits := float32bits(v)
		bytes := []byte{
			byte(bits),
			byte(bits >> 8),
			byte(bits >> 16),
			byte(bits >> 24),
		}
		_, err := w.Write(bytes)
		return err
	default:
		return fmt.Errorf("unsupported type: %T", v)
	}
}

func float32frombits(bits uint32) float32 {
	return *(*float32)(unsafe.Pointer(&bits))
}

func float32bits(f float32) uint32 {
	return *(*uint32)(unsafe.Pointer(&f))
}

type ModelZoo struct {
	models   map[string]*ModelInfo
	cacheDir string
}

type ModelInfo struct {
	Name         string
	Description  string
	Architecture string
	EmbeddingDim int
	URL          string
	Size         int64
	Checksum     string
}

func NewModelZoo(cacheDir string) *ModelZoo {
	zoo := &ModelZoo{
		models:   make(map[string]*ModelInfo),
		cacheDir: cacheDir,
	}

	zoo.registerBuiltinModels()

	return zoo
}

func (mz *ModelZoo) registerBuiltinModels() {
	mz.models["sentence-transformer-base"] = &ModelInfo{
		Name:         "sentence-transformer-base",
		Description:  "Base sentence transformer model for general semantic search",
		Architecture: "transformer",
		EmbeddingDim: 384,
		URL:          "",
		Size:         100 * 1024 * 1024,
		Checksum:     "",
	}

	mz.models["sentence-transformer-large"] = &ModelInfo{
		Name:         "sentence-transformer-large",
		Description:  "Large sentence transformer model for high-quality embeddings",
		Architecture: "transformer",
		EmbeddingDim: 768,
		URL:          "",
		Size:         400 * 1024 * 1024,
		Checksum:     "",
	}

	mz.models["universal-sentence-encoder"] = &ModelInfo{
		Name:         "universal-sentence-encoder",
		Description:  "Universal sentence encoder compatible model",
		Architecture: "transformer",
		EmbeddingDim: 512,
		URL:          "",
		Size:         250 * 1024 * 1024,
		Checksum:     "",
	}
}

func (mz *ModelZoo) ListModels() []string {
	models := make([]string, 0, len(mz.models))
	for name := range mz.models {
		models = append(models, name)
	}
	return models
}

func (mz *ModelZoo) GetModelInfo(name string) (*ModelInfo, error) {
	info, exists := mz.models[name]
	if !exists {
		return nil, fmt.Errorf("model %s not found", name)
	}
	return info, nil
}

func (mz *ModelZoo) LoadModel(name string, loader ModelLoader) (*PretrainedModel, error) {
	info, err := mz.GetModelInfo(name)
	if err != nil {
		return nil, err
	}

	modelPath := filepath.Join(mz.cacheDir, name)

	if _, err := os.Stat(modelPath); os.IsNotExist(err) {
		if err := mz.downloadModel(info, modelPath); err != nil {
			return nil, fmt.Errorf("failed to download model: %w", err)
		}
	}

	return loader.Load(modelPath)
}

func (mz *ModelZoo) downloadModel(info *ModelInfo, path string) error {
	if info.URL == "" {
		return fmt.Errorf("model %s does not have a download URL", info.Name)
	}

	return fmt.Errorf("model downloading not yet implemented")
}

type ModelConverter struct {
	sourceFormat ModelFormat
	targetFormat ModelFormat
}

func NewModelConverter(source, target ModelFormat) *ModelConverter {
	return &ModelConverter{
		sourceFormat: source,
		targetFormat: target,
	}
}

func (mc *ModelConverter) Convert(sourcePath, targetPath string) error {
	return fmt.Errorf("model conversion not yet implemented")
}

func LoadSentenceEncoder(modelPath string, embeddingDim int) (*SentenceEncoder, error) {
	loader := NewCustomModelLoader()
	pretrainedModel, err := loader.Load(modelPath)
	if err != nil {
		return nil, err
	}

	if pretrainedModel.EmbeddingDim != embeddingDim {
		return nil, fmt.Errorf("embedding dimension mismatch: model has %d, requested %d",
			pretrainedModel.EmbeddingDim, embeddingDim)
	}

	encoder := NewSentenceEncoder(embeddingDim, MeanPooling)

	if err := applyWeights(encoder, pretrainedModel.Weights); err != nil {
		return nil, fmt.Errorf("failed to apply weights: %w", err)
	}

	return encoder, nil
}

func applyWeights(encoder *SentenceEncoder, weights map[string]*utils.Tensor) error {
	if embeddings, exists := weights["token_embeddings"]; exists {
		encoder.tokenEmbedding.embeddings = embeddings
	}

	for i, block := range encoder.transformerBlocks {
		prefix := fmt.Sprintf("transformer.layer.%d", i)

		if qWeight, exists := weights[prefix+".attention.q_linear.weight"]; exists {
			block.attention.qLinear.weight = qWeight
		}
		if qBias, exists := weights[prefix+".attention.q_linear.bias"]; exists {
			block.attention.qLinear.bias = qBias
		}

		if kWeight, exists := weights[prefix+".attention.k_linear.weight"]; exists {
			block.attention.kLinear.weight = kWeight
		}
		if kBias, exists := weights[prefix+".attention.k_linear.bias"]; exists {
			block.attention.kLinear.bias = kBias
		}

		if vWeight, exists := weights[prefix+".attention.v_linear.weight"]; exists {
			block.attention.vLinear.weight = vWeight
		}
		if vBias, exists := weights[prefix+".attention.v_linear.bias"]; exists {
			block.attention.vLinear.bias = vBias
		}

		if outWeight, exists := weights[prefix+".attention.out_linear.weight"]; exists {
			block.attention.outLinear.weight = outWeight
		}
		if outBias, exists := weights[prefix+".attention.out_linear.bias"]; exists {
			block.attention.outLinear.bias = outBias
		}

		if ff1Weight, exists := weights[prefix+".feedforward.linear1.weight"]; exists {
			block.feedForward.linear1.weight = ff1Weight
		}
		if ff1Bias, exists := weights[prefix+".feedforward.linear1.bias"]; exists {
			block.feedForward.linear1.bias = ff1Bias
		}

		if ff2Weight, exists := weights[prefix+".feedforward.linear2.weight"]; exists {
			block.feedForward.linear2.weight = ff2Weight
		}
		if ff2Bias, exists := weights[prefix+".feedforward.linear2.bias"]; exists {
			block.feedForward.linear2.bias = ff2Bias
		}

		if ln1Gamma, exists := weights[prefix+".layernorm1.gamma"]; exists {
			block.layerNorm1.gamma = ln1Gamma
		}
		if ln1Beta, exists := weights[prefix+".layernorm1.beta"]; exists {
			block.layerNorm1.beta = ln1Beta
		}

		if ln2Gamma, exists := weights[prefix+".layernorm2.gamma"]; exists {
			block.layerNorm2.gamma = ln2Gamma
		}
		if ln2Beta, exists := weights[prefix+".layernorm2.beta"]; exists {
			block.layerNorm2.beta = ln2Beta
		}
	}

	if outWeight, exists := weights["output_projection.weight"]; exists {
		encoder.outputProjection.weight = outWeight
	}
	if outBias, exists := weights["output_projection.bias"]; exists {
		encoder.outputProjection.bias = outBias
	}

	return nil
}
