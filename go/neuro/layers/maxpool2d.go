package layers

import (
	"fmt"

	"github.com/muchq/moonbase/go/neuro/utils"
)

// MaxPool2D performs 2D max pooling on 4D input tensors
type MaxPool2D struct {
	PoolSize   []int   // [height, width] of pooling window
	Stride     int     // Stride for pooling window
	Padding    string  // "valid" or "same"
	input      *utils.Tensor // Cache input for backward pass
	indices    []int   // Indices of max values for gradient routing
	inputShape []int   // Original input shape
}

// NewMaxPool2D creates a new MaxPool2D layer
func NewMaxPool2D(poolSize []int, stride int, padding string) *MaxPool2D {
	if len(poolSize) != 2 {
		panic(fmt.Sprintf("MaxPool2D: poolSize must have 2 elements, got %d", len(poolSize)))
	}
	if stride <= 0 {
		panic(fmt.Sprintf("MaxPool2D: stride must be positive, got %d", stride))
	}
	if padding != "valid" && padding != "same" {
		panic(fmt.Sprintf("MaxPool2D: padding must be 'valid' or 'same', got '%s'", padding))
	}

	return &MaxPool2D{
		PoolSize: poolSize,
		Stride:   stride,
		Padding:  padding,
	}
}

// Forward performs max pooling on the input
// Input shape: (batch, channels, height, width)
// Output shape: (batch, channels, out_height, out_width)
func (m *MaxPool2D) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	if len(input.Shape) != 4 {
		panic(fmt.Sprintf("MaxPool2D: expected 4D input (batch, channels, height, width), got shape %v", input.Shape))
	}

	// Cache input for backward pass
	m.input = input
	m.inputShape = input.Shape

	batch, channels, height, width := input.Shape[0], input.Shape[1], input.Shape[2], input.Shape[3]
	poolH, poolW := m.PoolSize[0], m.PoolSize[1]

	// Calculate padding if needed
	padTop, padBottom, padLeft, padRight := 0, 0, 0, 0
	if m.Padding == "same" {
		padTop, padBottom = utils.CalculateSamePaddingAsymmetric(height, poolH, m.Stride)
		padLeft, padRight = utils.CalculateSamePaddingAsymmetric(width, poolW, m.Stride)
	}

	// Apply padding if needed
	paddedInput := input
	if padTop > 0 || padBottom > 0 || padLeft > 0 || padRight > 0 {
		paddedInput = input.Pad2DAsymmetric(padTop, padBottom, padLeft, padRight)
		height += padTop + padBottom
		width += padLeft + padRight
	}

	// Calculate output dimensions
	outH := (height - poolH) / m.Stride + 1
	outW := (width - poolW) / m.Stride + 1

	// Create output tensor
	output := utils.NewTensor(batch, channels, outH, outW)
	m.indices = make([]int, batch*channels*outH*outW)

	// Perform max pooling
	for b := 0; b < batch; b++ {
		for c := 0; c < channels; c++ {
			for oh := 0; oh < outH; oh++ {
				for ow := 0; ow < outW; ow++ {
					// Find max in pooling window
					maxVal := paddedInput.Get4D(b, c, oh*m.Stride, ow*m.Stride)
					maxH, maxW := oh*m.Stride, ow*m.Stride

					for ph := 0; ph < poolH; ph++ {
						for pw := 0; pw < poolW; pw++ {
							h := oh*m.Stride + ph
							w := ow*m.Stride + pw
							if h < height && w < width {
								val := paddedInput.Get4D(b, c, h, w)
								if val > maxVal {
									maxVal = val
									maxH, maxW = h, w
								}
							}
						}
					}

					// Store max value and its position
					output.Set4D(b, c, oh, ow, maxVal)
					
					// Store index in original (unpadded) input coordinates
					origH := maxH - padTop
					origW := maxW - padLeft
					if origH >= 0 && origH < m.inputShape[2] && origW >= 0 && origW < m.inputShape[3] {
						m.indices[b*channels*outH*outW + c*outH*outW + oh*outW + ow] = 
							b*channels*m.inputShape[2]*m.inputShape[3] + 
							c*m.inputShape[2]*m.inputShape[3] + 
							origH*m.inputShape[3] + origW
					} else {
						// This was a padded value, set to -1
						m.indices[b*channels*outH*outW + c*outH*outW + oh*outW + ow] = -1
					}
				}
			}
		}
	}

	return output
}

// Backward computes gradients for max pooling
// Routes gradients only to positions where max values were selected
func (m *MaxPool2D) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	if m.input == nil {
		panic("MaxPool2D: Backward called before Forward")
	}

	// Create gradient tensor with same shape as input
	gradInput := utils.NewTensor(m.inputShape...)

	// Route gradients to max positions
	for i, idx := range m.indices {
		if idx >= 0 && idx < len(gradInput.Data) {
			gradInput.Data[idx] += gradOutput.Data[i]
		}
	}

	return gradInput
}

// UpdateWeights does nothing as MaxPool2D has no parameters
func (m *MaxPool2D) UpdateWeights(lr float64) {
	// No parameters to update
}

// GetParams returns empty slice as MaxPool2D has no parameters
func (m *MaxPool2D) GetParams() []*utils.Tensor {
	return []*utils.Tensor{}
}

// GetGradients returns empty slice as MaxPool2D has no parameters
func (m *MaxPool2D) GetGradients() []*utils.Tensor {
	return []*utils.Tensor{}
}

// SetParams does nothing as MaxPool2D has no parameters
func (m *MaxPool2D) SetParams(params []*utils.Tensor) {
	if len(params) != 0 {
		panic(fmt.Sprintf("MaxPool2D: Expected 0 parameters, got %d", len(params)))
	}
}

// Name returns the layer name
func (m *MaxPool2D) Name() string {
	return fmt.Sprintf("MaxPool2D(pool=%dx%d, stride=%d, padding=%s)", 
		m.PoolSize[0], m.PoolSize[1], m.Stride, m.Padding)
}