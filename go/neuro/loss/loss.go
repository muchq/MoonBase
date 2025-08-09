package loss

import (
	"math"
	
	"github.com/MoonBase/go/neuro/utils"
)

type Loss interface {
	Forward(predictions, targets *utils.Tensor) float64
	Backward(predictions, targets *utils.Tensor) *utils.Tensor
	Name() string
}

type MeanSquaredError struct{}

func NewMSE() *MeanSquaredError {
	return &MeanSquaredError{}
}

func (m *MeanSquaredError) Forward(predictions, targets *utils.Tensor) float64 {
	diff := predictions.Sub(targets)
	squared := diff.Mul(diff)
	return squared.Mean()
}

func (m *MeanSquaredError) Backward(predictions, targets *utils.Tensor) *utils.Tensor {
	n := float64(len(predictions.Data))
	diff := predictions.Sub(targets)
	return diff.Scale(2.0 / n)
}

func (m *MeanSquaredError) Name() string {
	return "MSE"
}

type CrossEntropy struct{}

func NewCrossEntropy() *CrossEntropy {
	return &CrossEntropy{}
}

func (c *CrossEntropy) Forward(predictions, targets *utils.Tensor) float64 {
	epsilon := 1e-7
	loss := 0.0
	
	for i := range predictions.Data {
		pred := math.Max(epsilon, math.Min(1-epsilon, predictions.Data[i]))
		if targets.Data[i] == 1.0 {
			loss -= math.Log(pred)
		} else {
			loss -= math.Log(1 - pred)
		}
	}
	
	return loss / float64(predictions.Shape[0])
}

func (c *CrossEntropy) Backward(predictions, targets *utils.Tensor) *utils.Tensor {
	epsilon := 1e-7
	n := float64(predictions.Shape[0])
	grad := utils.NewTensor(predictions.Shape...)
	
	for i := range grad.Data {
		pred := math.Max(epsilon, math.Min(1-epsilon, predictions.Data[i]))
		if targets.Data[i] == 1.0 {
			grad.Data[i] = -1.0 / pred / n
		} else {
			grad.Data[i] = 1.0 / (1 - pred) / n
		}
	}
	
	return grad
}

func (c *CrossEntropy) Name() string {
	return "CrossEntropy"
}

type CategoricalCrossEntropy struct{}

func NewCategoricalCrossEntropy() *CategoricalCrossEntropy {
	return &CategoricalCrossEntropy{}
}

func (c *CategoricalCrossEntropy) Forward(predictions, targets *utils.Tensor) float64 {
	epsilon := 1e-7
	loss := 0.0
	
	for i := range predictions.Data {
		if targets.Data[i] > 0 {
			pred := math.Max(epsilon, predictions.Data[i])
			loss -= targets.Data[i] * math.Log(pred)
		}
	}
	
	if len(predictions.Shape) == 2 {
		return loss / float64(predictions.Shape[0])
	}
	return loss
}

func (c *CategoricalCrossEntropy) Backward(predictions, targets *utils.Tensor) *utils.Tensor {
	epsilon := 1e-7
	grad := utils.NewTensor(predictions.Shape...)
	
	n := 1.0
	if len(predictions.Shape) == 2 {
		n = float64(predictions.Shape[0])
	}
	
	for i := range grad.Data {
		if targets.Data[i] > 0 {
			pred := math.Max(epsilon, predictions.Data[i])
			grad.Data[i] = -targets.Data[i] / pred / n
		}
	}
	
	return grad
}

func (c *CategoricalCrossEntropy) Name() string {
	return "CategoricalCrossEntropy"
}