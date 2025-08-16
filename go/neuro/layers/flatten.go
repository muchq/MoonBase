package layers

import (
	"fmt"

	"github.com/muchq/moonbase/go/neuro/utils"
)

// Flatten layer reshapes multi-dimensional input to 2D (batch_size, features)
// Commonly used between convolutional and dense layers
type Flatten struct {
	originalShape []int         // Store original shape for backward pass
	input         *utils.Tensor // Cache input for backward pass
}

// NewFlatten creates a new Flatten layer
func NewFlatten() *Flatten {
	return &Flatten{}
}

// Forward reshapes input from (batch, ...) to (batch, features)
func (f *Flatten) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	// Cache input for backward pass
	f.input = input
	f.originalShape = input.Shape

	// Calculate flattened shape
	batchSize := input.Shape[0]
	features := 1
	for i := 1; i < len(input.Shape); i++ {
		features *= input.Shape[i]
	}

	// Create new shape (batch_size, features)
	newShape := []int{batchSize, features}

	// Reshape and return
	return input.Reshape(newShape...)
}

// Backward reshapes gradients back to original input shape
func (f *Flatten) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	if f.originalShape == nil {
		panic("Flatten: Backward called before Forward")
	}

	// Reshape gradients back to original shape
	return gradOutput.Reshape(f.originalShape...)
}

// UpdateWeights does nothing as Flatten has no parameters
func (f *Flatten) UpdateWeights(lr float64) {
	// No parameters to update
}

// GetParams returns empty slice as Flatten has no parameters
func (f *Flatten) GetParams() []*utils.Tensor {
	return []*utils.Tensor{}
}

// GetGradients returns empty slice as Flatten has no parameters
func (f *Flatten) GetGradients() []*utils.Tensor {
	return []*utils.Tensor{}
}

// SetParams does nothing as Flatten has no parameters
func (f *Flatten) SetParams(params []*utils.Tensor) {
	if len(params) != 0 {
		panic(fmt.Sprintf("Flatten: Expected 0 parameters, got %d", len(params)))
	}
}

// Name returns the layer name
func (f *Flatten) Name() string {
	return "Flatten"
}