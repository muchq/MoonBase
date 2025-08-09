package network

import (
	"fmt"
	
	"github.com/MoonBase/go/neuro/layers"
	"github.com/MoonBase/go/neuro/loss"
	"github.com/MoonBase/go/neuro/utils"
)

type Model struct {
	layers    []layers.Layer
	loss      loss.Loss
	optimizer Optimizer
}

func NewModel() *Model {
	return &Model{
		layers: []layers.Layer{},
	}
}

func (m *Model) Add(layer layers.Layer) {
	m.layers = append(m.layers, layer)
}

func (m *Model) SetLoss(lossFunc loss.Loss) {
	m.loss = lossFunc
}

func (m *Model) SetOptimizer(opt Optimizer) {
	m.optimizer = opt
	opt.SetLayers(m.layers)
}

func (m *Model) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	output := input
	for _, layer := range m.layers {
		output = layer.Forward(output, training)
	}
	return output
}

func (m *Model) Backward(gradOutput *utils.Tensor) {
	grad := gradOutput
	for i := len(m.layers) - 1; i >= 0; i-- {
		grad = m.layers[i].Backward(grad)
	}
}

func (m *Model) Train(x, y *utils.Tensor) float64 {
	predictions := m.Forward(x, true)
	
	lossValue := m.loss.Forward(predictions, y)
	
	gradOutput := m.loss.Backward(predictions, y)
	m.Backward(gradOutput)
	
	if m.optimizer != nil {
		m.optimizer.Step()
	}
	
	return lossValue
}

func (m *Model) Predict(x *utils.Tensor) *utils.Tensor {
	return m.Forward(x, false)
}

func (m *Model) Evaluate(x, y *utils.Tensor) (float64, float64) {
	predictions := m.Predict(x)
	lossValue := m.loss.Forward(predictions, y)
	
	accuracy := m.calculateAccuracy(predictions, y)
	
	return lossValue, accuracy
}

func (m *Model) calculateAccuracy(predictions, targets *utils.Tensor) float64 {
	if len(predictions.Shape) != 2 {
		return 0.0
	}
	
	correct := 0
	batchSize := predictions.Shape[0]
	numClasses := predictions.Shape[1]
	
	for i := 0; i < batchSize; i++ {
		predMaxIdx := 0
		predMaxVal := predictions.Get(i, 0)
		targetMaxIdx := 0
		targetMaxVal := targets.Get(i, 0)
		
		for j := 1; j < numClasses; j++ {
			if predictions.Get(i, j) > predMaxVal {
				predMaxVal = predictions.Get(i, j)
				predMaxIdx = j
			}
			if targets.Get(i, j) > targetMaxVal {
				targetMaxVal = targets.Get(i, j)
				targetMaxIdx = j
			}
		}
		
		if predMaxIdx == targetMaxIdx {
			correct++
		}
	}
	
	return float64(correct) / float64(batchSize)
}

func (m *Model) Summary() {
	fmt.Println("Model Summary:")
	fmt.Println("==============")
	for i, layer := range m.layers {
		fmt.Printf("Layer %d: %s\n", i+1, layer.Name())
	}
	if m.loss != nil {
		fmt.Printf("Loss: %s\n", m.loss.Name())
	}
	if m.optimizer != nil {
		fmt.Printf("Optimizer: %s\n", m.optimizer.Name())
	}
}

func (m *Model) GetParams() [][]float64 {
	params := [][]float64{}
	for _, layer := range m.layers {
		layerParams := layer.GetParams()
		for _, tensor := range layerParams {
			params = append(params, tensor.Data)
		}
	}
	return params
}

func (m *Model) SetParams(params [][]float64) {
	idx := 0
	for _, layer := range m.layers {
		layerParams := layer.GetParams()
		tensors := make([]*utils.Tensor, len(layerParams))
		for i, tensor := range layerParams {
			data := params[idx]
			newTensor := utils.NewTensor(tensor.Shape...)
			copy(newTensor.Data, data)
			tensors[i] = newTensor
			idx++
		}
		layer.SetParams(tensors)
	}
}

func (m *Model) GetLayers() []layers.Layer {
	return m.layers
}