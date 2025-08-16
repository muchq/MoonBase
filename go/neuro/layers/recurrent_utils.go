package layers

import (
	"math"
	
	"github.com/muchq/moonbase/go/neuro/utils"
)

// Sigmoid activation with numerical stability
func sigmoidStable(x *utils.Tensor) *utils.Tensor {
	result := x.Copy()
	for i := range result.Data {
		// Clip input to prevent overflow
		val := math.Max(math.Min(result.Data[i], 20.0), -20.0)
		result.Data[i] = 1.0 / (1.0 + math.Exp(-val))
	}
	return result
}

// Sigmoid backward pass
func sigmoidBackwardStable(grad, output *utils.Tensor) *utils.Tensor {
	result := grad.Copy()
	for i := range result.Data {
		result.Data[i] *= output.Data[i] * (1.0 - output.Data[i])
	}
	return result
}

// Tanh activation
func tanhActivation(x *utils.Tensor) *utils.Tensor {
	result := x.Copy()
	for i := range result.Data {
		result.Data[i] = math.Tanh(result.Data[i])
	}
	return result
}

// Tanh backward pass
func tanhBackwardActivation(grad, output *utils.Tensor) *utils.Tensor {
	result := grad.Copy()
	for i := range result.Data {
		result.Data[i] *= (1.0 - output.Data[i]*output.Data[i])
	}
	return result
}