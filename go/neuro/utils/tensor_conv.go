package utils

import (
	"fmt"
)

// PaddingType defines the type of padding for convolution operations
type PaddingType string

const (
	PaddingValid PaddingType = "valid" // No padding
	PaddingSame  PaddingType = "same"  // Pad to keep output size same as input
)

// CalculateConvOutputSize calculates output dimensions for convolution/pooling
func CalculateConvOutputSize(inputSize, kernelSize, stride, padding int) int {
	return (inputSize + 2*padding - kernelSize) / stride + 1
}


// CalculateSamePaddingAsymmetric calculates asymmetric padding for 'same' convolution
// Returns (padLeft, padRight) or (padTop, padBottom) depending on dimension
// This properly handles even kernel sizes by distributing padding asymmetrically
func CalculateSamePaddingAsymmetric(inputSize, kernelSize, stride int) (int, int) {
	// For 'same' padding, we want output size = ceil(inputSize / stride)
	outputSize := (inputSize + stride - 1) / stride
	
	// Calculate total padding needed
	// From formula: outputSize = (inputSize + totalPadding - kernelSize) / stride + 1
	// Rearranging: totalPadding = (outputSize - 1) * stride + kernelSize - inputSize
	totalPadding := (outputSize - 1) * stride + kernelSize - inputSize
	
	if totalPadding < 0 {
		totalPadding = 0
	}
	
	// Distribute padding: left/top gets floor, right/bottom gets ceil
	padLeft := totalPadding / 2
	padRight := totalPadding - padLeft
	
	return padLeft, padRight
}

// Pad2D adds zero padding to a 4D tensor (batch, channels, height, width)
// Uses symmetric padding (same amount on both sides)
func (t *Tensor) Pad2D(padH, padW int) *Tensor {
	return t.Pad2DAsymmetric(padH, padH, padW, padW)
}

// Pad2DAsymmetric adds asymmetric zero padding to a 4D tensor
// Allows different padding amounts for each side
func (t *Tensor) Pad2DAsymmetric(padTop, padBottom, padLeft, padRight int) *Tensor {
	if len(t.Shape) != 4 {
		panic(fmt.Sprintf("Pad2DAsymmetric requires 4D tensor, got shape %v", t.Shape))
	}

	batch, channels, height, width := t.Shape[0], t.Shape[1], t.Shape[2], t.Shape[3]
	newHeight := height + padTop + padBottom
	newWidth := width + padLeft + padRight

	// Create new padded tensor
	padded := NewTensor(batch, channels, newHeight, newWidth)

	// Copy original data to padded tensor
	for b := 0; b < batch; b++ {
		for c := 0; c < channels; c++ {
			for h := 0; h < height; h++ {
				for w := 0; w < width; w++ {
					srcIdx := b*channels*height*width + c*height*width + h*width + w
					dstIdx := b*channels*newHeight*newWidth + c*newHeight*newWidth + (h+padTop)*newWidth + (w+padLeft)
					padded.Data[dstIdx] = t.Data[srcIdx]
				}
			}
		}
	}

	return padded
}

// Im2Col transforms image patches to columns for efficient convolution
// Input: 4D tensor (batch, channels, height, width)
// Output: 2D tensor (batch*out_h*out_w, channels*kernel_h*kernel_w)
func (t *Tensor) Im2Col(kernelH, kernelW, stride, padH, padW int) *Tensor {
	return t.Im2ColAsymmetric(kernelH, kernelW, stride, padH, padH, padW, padW)
}

// Im2ColAsymmetric transforms image patches to columns with asymmetric padding
func (t *Tensor) Im2ColAsymmetric(kernelH, kernelW, stride, padTop, padBottom, padLeft, padRight int) *Tensor {
	if len(t.Shape) != 4 {
		panic(fmt.Sprintf("Im2ColAsymmetric requires 4D tensor, got shape %v", t.Shape))
	}

	batch, channels, height, width := t.Shape[0], t.Shape[1], t.Shape[2], t.Shape[3]

	// Add padding if needed
	var paddedInput *Tensor
	if padTop > 0 || padBottom > 0 || padLeft > 0 || padRight > 0 {
		paddedInput = t.Pad2DAsymmetric(padTop, padBottom, padLeft, padRight)
		height += padTop + padBottom
		width += padLeft + padRight
	} else {
		paddedInput = t
	}

	// Calculate output dimensions
	outH := (height - kernelH) / stride + 1
	outW := (width - kernelW) / stride + 1

	// Create column matrix
	colHeight := batch * outH * outW
	colWidth := channels * kernelH * kernelW
	col := NewTensor(colHeight, colWidth)

	// Fill column matrix
	colIdx := 0
	for b := 0; b < batch; b++ {
		for h := 0; h <= height-kernelH; h += stride {
			for w := 0; w <= width-kernelW; w += stride {
				// Extract patch and flatten to column
				patchIdx := 0
				for c := 0; c < channels; c++ {
					for kh := 0; kh < kernelH; kh++ {
						for kw := 0; kw < kernelW; kw++ {
							imgIdx := b*channels*height*width + c*height*width + (h+kh)*width + (w + kw)
							col.Data[colIdx*colWidth+patchIdx] = paddedInput.Data[imgIdx]
							patchIdx++
						}
					}
				}
				colIdx++
			}
		}
	}

	return col
}

// Col2Im transforms columns back to image format (inverse of Im2Col)
// Input: 2D tensor (batch*out_h*out_w, channels*kernel_h*kernel_w)
// Output: 4D tensor (batch, channels, height, width)
func Col2Im(col *Tensor, batch, channels, height, width, kernelH, kernelW, stride, padH, padW int) *Tensor {
	return Col2ImAsymmetric(col, batch, channels, height, width, kernelH, kernelW, stride, padH, padH, padW, padW)
}

// Col2ImAsymmetric transforms columns back to image format with asymmetric padding
func Col2ImAsymmetric(col *Tensor, batch, channels, height, width, kernelH, kernelW, stride, padTop, padBottom, padLeft, padRight int) *Tensor {
	if len(col.Shape) != 2 {
		panic(fmt.Sprintf("Col2ImAsymmetric requires 2D tensor, got shape %v", col.Shape))
	}

	// Calculate padded dimensions
	paddedHeight := height + padTop + padBottom
	paddedWidth := width + padLeft + padRight

	// Calculate output dimensions (unused but kept for clarity)
	_ = (paddedHeight - kernelH) / stride + 1
	_ = (paddedWidth - kernelW) / stride + 1

	// Create output tensor (with padding)
	img := NewTensor(batch, channels, paddedHeight, paddedWidth)

	// Fill image from columns
	colIdx := 0
	for b := 0; b < batch; b++ {
		for h := 0; h <= paddedHeight-kernelH; h += stride {
			for w := 0; w <= paddedWidth-kernelW; w += stride {
				// Add column values back to image
				patchIdx := 0
				for c := 0; c < channels; c++ {
					for kh := 0; kh < kernelH; kh++ {
						for kw := 0; kw < kernelW; kw++ {
							imgIdx := b*channels*paddedHeight*paddedWidth + c*paddedHeight*paddedWidth + (h+kh)*paddedWidth + (w + kw)
							img.Data[imgIdx] += col.Data[colIdx*col.Shape[1]+patchIdx]
							patchIdx++
						}
					}
				}
				colIdx++
			}
		}
	}

	// Remove padding if it was added
	if padTop > 0 || padBottom > 0 || padLeft > 0 || padRight > 0 {
		// Create unpadded tensor
		unpadded := NewTensor(batch, channels, height, width)
		for b := 0; b < batch; b++ {
			for c := 0; c < channels; c++ {
				for h := 0; h < height; h++ {
					for w := 0; w < width; w++ {
						srcIdx := b*channels*paddedHeight*paddedWidth + c*paddedHeight*paddedWidth + (h+padTop)*paddedWidth + (w+padLeft)
						dstIdx := b*channels*height*width + c*height*width + h*width + w
						unpadded.Data[dstIdx] = img.Data[srcIdx]
					}
				}
			}
		}
		return unpadded
	}

	return img
}

// Get4D provides convenient access to 4D tensor elements
func (t *Tensor) Get4D(b, c, h, w int) float64 {
	if len(t.Shape) != 4 {
		panic(fmt.Sprintf("Get4D requires 4D tensor, got shape %v", t.Shape))
	}
	idx := b*t.Shape[1]*t.Shape[2]*t.Shape[3] + c*t.Shape[2]*t.Shape[3] + h*t.Shape[3] + w
	return t.Data[idx]
}

// Set4D provides convenient setting of 4D tensor elements
func (t *Tensor) Set4D(b, c, h, w int, value float64) {
	if len(t.Shape) != 4 {
		panic(fmt.Sprintf("Set4D requires 4D tensor, got shape %v", t.Shape))
	}
	idx := b*t.Shape[1]*t.Shape[2]*t.Shape[3] + c*t.Shape[2]*t.Shape[3] + h*t.Shape[3] + w
	t.Data[idx] = value
}

// TransposeAxes permutes the dimensions of a 4D tensor according to the given axes
func (t *Tensor) TransposeAxes(axis0, axis1, axis2, axis3 int) *Tensor {
	if len(t.Shape) != 4 {
		panic(fmt.Sprintf("TransposeAxes requires 4D tensor, got shape %v", t.Shape))
	}
	
	axes := []int{axis0, axis1, axis2, axis3}
	newShape := []int{t.Shape[axes[0]], t.Shape[axes[1]], t.Shape[axes[2]], t.Shape[axes[3]]}
	result := NewTensor(newShape...)
	
	// Compute strides for original tensor
	oldStrides := make([]int, 4)
	oldStrides[3] = 1
	for i := 2; i >= 0; i-- {
		oldStrides[i] = oldStrides[i+1] * t.Shape[i+1]
	}
	
	// Compute strides for new tensor
	newStrides := make([]int, 4)
	newStrides[3] = 1
	for i := 2; i >= 0; i-- {
		newStrides[i] = newStrides[i+1] * newShape[i+1]
	}
	
	// Perform transposition
	for i0 := 0; i0 < t.Shape[0]; i0++ {
		for i1 := 0; i1 < t.Shape[1]; i1++ {
			for i2 := 0; i2 < t.Shape[2]; i2++ {
				for i3 := 0; i3 < t.Shape[3]; i3++ {
					oldIndices := []int{i0, i1, i2, i3}
					newIndices := []int{oldIndices[axes[0]], oldIndices[axes[1]], oldIndices[axes[2]], oldIndices[axes[3]]}
					
					oldIdx := i0*oldStrides[0] + i1*oldStrides[1] + i2*oldStrides[2] + i3*oldStrides[3]
					newIdx := newIndices[0]*newStrides[0] + newIndices[1]*newStrides[1] + 
							  newIndices[2]*newStrides[2] + newIndices[3]*newStrides[3]
					
					result.Data[newIdx] = t.Data[oldIdx]
				}
			}
		}
	}
	
	return result
}

// MaxPool2DIndices performs max pooling and returns both output and indices
// Used internally by MaxPool2D layer for gradient routing
func (t *Tensor) MaxPool2DIndices(poolH, poolW, stride int) (*Tensor, []int) {
	if len(t.Shape) != 4 {
		panic(fmt.Sprintf("MaxPool2DIndices requires 4D tensor, got shape %v", t.Shape))
	}

	batch, channels, height, width := t.Shape[0], t.Shape[1], t.Shape[2], t.Shape[3]

	// Calculate output dimensions
	outH := (height - poolH) / stride + 1
	outW := (width - poolW) / stride + 1

	// Create output tensor and indices array
	output := NewTensor(batch, channels, outH, outW)
	indices := make([]int, batch*channels*outH*outW)

	// Perform max pooling
	for b := 0; b < batch; b++ {
		for c := 0; c < channels; c++ {
			for oh := 0; oh < outH; oh++ {
				for ow := 0; ow < outW; ow++ {
					// Find max in pooling window
					maxVal := t.Get4D(b, c, oh*stride, ow*stride)
					maxIdx := oh*stride*width + ow*stride

					for ph := 0; ph < poolH; ph++ {
						for pw := 0; pw < poolW; pw++ {
							h := oh*stride + ph
							w := ow*stride + pw
							if h < height && w < width {
								val := t.Get4D(b, c, h, w)
								if val > maxVal {
									maxVal = val
									maxIdx = h*width + w
								}
							}
						}
					}

					// Store max value and index
					outIdx := b*channels*outH*outW + c*outH*outW + oh*outW + ow
					output.Data[outIdx] = maxVal
					indices[outIdx] = maxIdx
				}
			}
		}
	}

	return output, indices
}