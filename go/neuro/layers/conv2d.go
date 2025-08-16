package layers

import (
	"fmt"
	"math"
	"math/rand"

	"github.com/muchq/moonbase/go/neuro/utils"
)

// Conv2D performs 2D convolution on 4D input tensors
type Conv2D struct {
	InChannels  int           // Number of input channels
	OutChannels int           // Number of output channels (filters)
	KernelSize  []int         // [height, width] of convolution kernel
	Stride      int           // Stride for convolution
	Padding     string        // "valid" or "same"
	UseBias     bool          // Whether to use bias
	weights     *utils.Tensor // Shape: (out_channels, in_channels, kernel_h, kernel_w)
	bias        *utils.Tensor // Shape: (out_channels,)
	
	// Cache for backward pass
	input       *utils.Tensor
	colInput    *utils.Tensor
	gradWeights *utils.Tensor
	gradBias    *utils.Tensor
	padTop      int
	padBottom   int
	padLeft     int
	padRight    int
}

// NewConv2D creates a new Conv2D layer
func NewConv2D(inChannels, outChannels int, kernelSize []int, stride int, padding string, useBias bool) *Conv2D {
	if len(kernelSize) != 2 {
		panic(fmt.Sprintf("Conv2D: kernelSize must have 2 elements, got %d", len(kernelSize)))
	}
	if stride <= 0 {
		panic(fmt.Sprintf("Conv2D: stride must be positive, got %d", stride))
	}
	if padding != "valid" && padding != "same" {
		panic(fmt.Sprintf("Conv2D: padding must be 'valid' or 'same', got '%s'", padding))
	}

	conv := &Conv2D{
		InChannels:  inChannels,
		OutChannels: outChannels,
		KernelSize:  kernelSize,
		Stride:      stride,
		Padding:     padding,
		UseBias:     useBias,
	}

	// Initialize weights using He initialization (good for ReLU)
	conv.initializeWeights()

	return conv
}

// initializeWeights initializes the layer weights
func (c *Conv2D) initializeWeights() {
	kernelH, kernelW := c.KernelSize[0], c.KernelSize[1]
	
	// Create weight tensor
	c.weights = utils.NewTensor(c.OutChannels, c.InChannels, kernelH, kernelW)
	
	// He initialization: std = sqrt(2 / fan_in)
	fanIn := c.InChannels * kernelH * kernelW
	std := math.Sqrt(2.0 / float64(fanIn))
	
	for i := range c.weights.Data {
		c.weights.Data[i] = rand.NormFloat64() * std
	}
	
	// Initialize bias to zero if used
	if c.UseBias {
		c.bias = utils.NewTensor(c.OutChannels)
		// Bias initialized to zero by default
	}
	
	// Initialize gradient accumulators
	c.gradWeights = utils.NewTensor(c.weights.Shape...)
	if c.UseBias {
		c.gradBias = utils.NewTensor(c.bias.Shape...)
	}
}

// Forward performs the convolution operation
// Input shape: (batch, in_channels, height, width)
// Output shape: (batch, out_channels, out_height, out_width)
func (c *Conv2D) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	if len(input.Shape) != 4 {
		panic(fmt.Sprintf("Conv2D: expected 4D input (batch, channels, height, width), got shape %v", input.Shape))
	}
	if input.Shape[1] != c.InChannels {
		panic(fmt.Sprintf("Conv2D: expected %d input channels, got %d", c.InChannels, input.Shape[1]))
	}

	// Cache input for backward pass
	c.input = input
	
	batch, _, height, width := input.Shape[0], input.Shape[1], input.Shape[2], input.Shape[3]
	kernelH, kernelW := c.KernelSize[0], c.KernelSize[1]
	
	// Calculate padding
	c.padTop, c.padBottom, c.padLeft, c.padRight = 0, 0, 0, 0
	if c.Padding == "same" {
		c.padTop, c.padBottom = utils.CalculateSamePaddingAsymmetric(height, kernelH, c.Stride)
		c.padLeft, c.padRight = utils.CalculateSamePaddingAsymmetric(width, kernelW, c.Stride)
	}
	
	// Convert input to column matrix using Im2Col
	c.colInput = input.Im2ColAsymmetric(kernelH, kernelW, c.Stride, c.padTop, c.padBottom, c.padLeft, c.padRight)
	
	// Reshape weights for matrix multiplication
	// weights: (out_channels, in_channels * kernel_h * kernel_w)
	weightMatrix := c.weights.Reshape(c.OutChannels, c.InChannels * kernelH * kernelW)
	
	// Perform convolution as matrix multiplication
	// colInput: (batch * out_h * out_w, in_channels * kernel_h * kernel_w)
	// weightMatrix.T: (in_channels * kernel_h * kernel_w, out_channels)
	// Result: (batch * out_h * out_w, out_channels)
	colOutput := c.colInput.MatMul(weightMatrix.Transpose())
	
	// Calculate output dimensions
	paddedHeight := height + c.padTop + c.padBottom
	paddedWidth := width + c.padLeft + c.padRight
	outH := (paddedHeight - kernelH) / c.Stride + 1
	outW := (paddedWidth - kernelW) / c.Stride + 1
	
	// Reshape output to 4D
	output := colOutput.Reshape(batch, outH, outW, c.OutChannels)
	
	// Transpose to (batch, out_channels, out_h, out_w)
	output = output.TransposeAxes(0, 3, 1, 2)
	
	// Add bias if used
	if c.UseBias {
		// Broadcast bias across batch, height, and width dimensions
		for b := 0; b < batch; b++ {
			for oc := 0; oc < c.OutChannels; oc++ {
				biasVal := c.bias.Data[oc]
				for h := 0; h < outH; h++ {
					for w := 0; w < outW; w++ {
						idx := b*c.OutChannels*outH*outW + oc*outH*outW + h*outW + w
						output.Data[idx] += biasVal
					}
				}
			}
		}
	}
	
	return output
}

// Backward computes gradients for convolution
func (c *Conv2D) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	if c.input == nil {
		panic("Conv2D: Backward called before Forward")
	}
	
	batch := c.input.Shape[0]
	height, width := c.input.Shape[2], c.input.Shape[3]
	kernelH, kernelW := c.KernelSize[0], c.KernelSize[1]
	outH, outW := gradOutput.Shape[2], gradOutput.Shape[3]
	
	// Compute bias gradient if used
	if c.UseBias {
		// Sum gradients across batch, height, and width for each output channel
		for b := 0; b < batch; b++ {
			for oc := 0; oc < c.OutChannels; oc++ {
				for h := 0; h < outH; h++ {
					for w := 0; w < outW; w++ {
						c.gradBias.Data[oc] += gradOutput.Get4D(b, oc, h, w)
					}
				}
			}
		}
	}
	
	// Reshape gradient output for matrix operations
	// gradOutput: (batch, out_channels, out_h, out_w)
	// Need: (batch * out_h * out_w, out_channels)
	gradCol := gradOutput.TransposeAxes(0, 2, 3, 1)
	gradCol = gradCol.Reshape(batch * outH * outW, c.OutChannels)
	
	// Compute weight gradient
	// gradCol.T: (out_channels, batch * out_h * out_w)
	// colInput: (batch * out_h * out_w, in_channels * kernel_h * kernel_w)
	// Result: (out_channels, in_channels * kernel_h * kernel_w)
	gradWeightMatrix := gradCol.Transpose().MatMul(c.colInput)
	
	// Reshape and add to accumulated gradients
	gradWeightReshaped := gradWeightMatrix.Reshape(c.OutChannels, c.InChannels, kernelH, kernelW)
	for i := range c.gradWeights.Data {
		c.gradWeights.Data[i] += gradWeightReshaped.Data[i]
	}
	
	// Compute input gradient
	// gradCol: (batch * out_h * out_w, out_channels)
	// weights: (out_channels, in_channels * kernel_h * kernel_w)
	// Result: (batch * out_h * out_w, in_channels * kernel_h * kernel_w)
	weightMatrix := c.weights.Reshape(c.OutChannels, c.InChannels * kernelH * kernelW)
	gradColInput := gradCol.MatMul(weightMatrix)
	
	// Convert column gradient back to image format using Col2Im
	gradInput := utils.Col2ImAsymmetric(gradColInput, batch, c.InChannels, height, width, 
		kernelH, kernelW, c.Stride, c.padTop, c.padBottom, c.padLeft, c.padRight)
	
	return gradInput
}

// UpdateWeights updates the layer parameters using gradients
func (c *Conv2D) UpdateWeights(lr float64) {
	// Update weights
	for i := range c.weights.Data {
		c.weights.Data[i] -= lr * c.gradWeights.Data[i]
		c.gradWeights.Data[i] = 0 // Reset gradient
	}
	
	// Update bias if used
	if c.UseBias {
		for i := range c.bias.Data {
			c.bias.Data[i] -= lr * c.gradBias.Data[i]
			c.gradBias.Data[i] = 0 // Reset gradient
		}
	}
}

// GetParams returns the layer parameters
func (c *Conv2D) GetParams() []*utils.Tensor {
	if c.UseBias {
		return []*utils.Tensor{c.weights, c.bias}
	}
	return []*utils.Tensor{c.weights}
}

// GetGradients returns the layer gradients
func (c *Conv2D) GetGradients() []*utils.Tensor {
	if c.UseBias {
		return []*utils.Tensor{c.gradWeights, c.gradBias}
	}
	return []*utils.Tensor{c.gradWeights}
}

// SetParams sets the layer parameters
func (c *Conv2D) SetParams(params []*utils.Tensor) {
	expectedParams := 1
	if c.UseBias {
		expectedParams = 2
	}
	
	if len(params) != expectedParams {
		panic(fmt.Sprintf("Conv2D: Expected %d parameters, got %d", expectedParams, len(params)))
	}
	
	c.weights = params[0]
	if c.UseBias && len(params) > 1 {
		c.bias = params[1]
	}
	
	// Reinitialize gradient accumulators
	c.gradWeights = utils.NewTensor(c.weights.Shape...)
	if c.UseBias {
		c.gradBias = utils.NewTensor(c.bias.Shape...)
	}
}

// Name returns the layer name
func (c *Conv2D) Name() string {
	return fmt.Sprintf("Conv2D(in=%d, out=%d, kernel=%dx%d, stride=%d, padding=%s)", 
		c.InChannels, c.OutChannels, c.KernelSize[0], c.KernelSize[1], c.Stride, c.Padding)
}

