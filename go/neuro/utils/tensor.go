package utils

import (
	"fmt"
	"math"
	"math/rand"
)

type Tensor struct {
	Data   []float64
	Shape  []int
	Strides []int
}

func NewTensor(shape ...int) *Tensor {
	size := 1
	for _, s := range shape {
		size *= s
	}
	
	strides := make([]int, len(shape))
	stride := 1
	for i := len(shape) - 1; i >= 0; i-- {
		strides[i] = stride
		stride *= shape[i]
	}
	
	return &Tensor{
		Data:    make([]float64, size),
		Shape:   shape,
		Strides: strides,
	}
}

func NewTensorFromData(data []float64, shape ...int) *Tensor {
	t := NewTensor(shape...)
	copy(t.Data, data)
	return t
}

func (t *Tensor) Size() int {
	return len(t.Data)
}

func (t *Tensor) Get(indices ...int) float64 {
	idx := t.getIndex(indices...)
	return t.Data[idx]
}

func (t *Tensor) Set(value float64, indices ...int) {
	idx := t.getIndex(indices...)
	t.Data[idx] = value
}

func (t *Tensor) getIndex(indices ...int) int {
	if len(indices) != len(t.Shape) {
		panic(fmt.Sprintf("invalid indices: expected %d, got %d", len(t.Shape), len(indices)))
	}
	
	idx := 0
	for i, index := range indices {
		if index < 0 || index >= t.Shape[i] {
			panic(fmt.Sprintf("index out of bounds: %d not in [0, %d)", index, t.Shape[i]))
		}
		idx += index * t.Strides[i]
	}
	return idx
}

func (t *Tensor) Reshape(shape ...int) *Tensor {
	size := 1
	for _, s := range shape {
		size *= s
	}
	if size != len(t.Data) {
		panic(fmt.Sprintf("cannot reshape tensor of size %d to shape %v", len(t.Data), shape))
	}
	
	return NewTensorFromData(t.Data, shape...)
}

func (t *Tensor) Copy() *Tensor {
	newData := make([]float64, len(t.Data))
	copy(newData, t.Data)
	return &Tensor{
		Data:    newData,
		Shape:   append([]int{}, t.Shape...),
		Strides: append([]int{}, t.Strides...),
	}
}

func (t *Tensor) Add(other *Tensor) *Tensor {
	// Handle same shape
	if shapeEqual(t.Shape, other.Shape) {
		result := t.Copy()
		for i := range result.Data {
			result.Data[i] += other.Data[i]
		}
		return result
	}
	
	// Handle broadcasting when other is scalar (shape [1])
	if len(other.Shape) == 1 && other.Shape[0] == 1 {
		result := t.Copy()
		scalar := other.Data[0]
		for i := range result.Data {
			result.Data[i] += scalar
		}
		return result
	}
	
	// Handle broadcasting when other is 1D and t is 2D or 3D (broadcast along last dimension)
	if len(other.Shape) == 1 && other.Shape[0] == t.Shape[len(t.Shape)-1] {
		result := t.Copy()
		
		if len(t.Shape) == 2 {
			// 2D + 1D: (m, n) + (n,)
			for i := 0; i < t.Shape[0]; i++ {
				for j := 0; j < t.Shape[1]; j++ {
					idx := i*t.Shape[1] + j
					result.Data[idx] += other.Data[j]
				}
			}
		} else if len(t.Shape) == 3 {
			// 3D + 1D: (b, m, n) + (n,)
			for i := 0; i < t.Shape[0]; i++ {
				for j := 0; j < t.Shape[1]; j++ {
					for k := 0; k < t.Shape[2]; k++ {
						idx := i*t.Shape[1]*t.Shape[2] + j*t.Shape[2] + k
						result.Data[idx] += other.Data[k]
					}
				}
			}
		}
		return result
	}
	
	// Handle broadcasting when other has shape [..., 1]
	if len(t.Shape) == len(other.Shape) {
		canBroadcast := true
		for i := range other.Shape {
			if other.Shape[i] != 1 && other.Shape[i] != t.Shape[i] {
				canBroadcast = false
				break
			}
		}
		
		if canBroadcast {
			result := t.Copy()
			
			if len(t.Shape) == 2 {
				// 2D broadcasting
				for i := 0; i < t.Shape[0]; i++ {
					for j := 0; j < t.Shape[1]; j++ {
						otherI := i
						otherJ := j
						if other.Shape[0] == 1 {
							otherI = 0
						}
						if other.Shape[1] == 1 {
							otherJ = 0
						}
						idx := i*t.Shape[1] + j
						result.Data[idx] += other.Get(otherI, otherJ)
					}
				}
			} else if len(t.Shape) == 3 {
				// 3D broadcasting
				for i := 0; i < t.Shape[0]; i++ {
					for j := 0; j < t.Shape[1]; j++ {
						for k := 0; k < t.Shape[2]; k++ {
							otherI, otherJ, otherK := i, j, k
							if other.Shape[0] == 1 {
								otherI = 0
							}
							if other.Shape[1] == 1 {
								otherJ = 0
							}
							if other.Shape[2] == 1 {
								otherK = 0
							}
							idx := i*t.Shape[1]*t.Shape[2] + j*t.Shape[2] + k
							result.Data[idx] += other.Get(otherI, otherJ, otherK)
						}
					}
				}
			} else {
				panic("broadcasting for Add only supports 2D and 3D tensors")
			}
			
			return result
		}
	}
	
	panic(fmt.Sprintf("shapes must match or be broadcastable for addition: %v and %v", t.Shape, other.Shape))
}

func (t *Tensor) Sub(other *Tensor) *Tensor {
	// Handle broadcasting for common cases
	if shapeEqual(t.Shape, other.Shape) {
		// Same shape - element-wise subtraction
		result := t.Copy()
		for i := range result.Data {
			result.Data[i] -= other.Data[i]
		}
		return result
	}
	
	// Handle broadcasting when other has shape [..., 1]
	if len(t.Shape) == len(other.Shape) {
		canBroadcast := true
		for i := range other.Shape {
			if other.Shape[i] != 1 && other.Shape[i] != t.Shape[i] {
				canBroadcast = false
				break
			}
		}
		
		if canBroadcast {
			result := t.Copy()
			
			if len(t.Shape) == 2 {
				// 2D broadcasting
				for i := 0; i < t.Shape[0]; i++ {
					for j := 0; j < t.Shape[1]; j++ {
						otherI := i
						otherJ := j
						if other.Shape[0] == 1 {
							otherI = 0
						}
						if other.Shape[1] == 1 {
							otherJ = 0
						}
						idx := i*t.Shape[1] + j
						result.Data[idx] -= other.Get(otherI, otherJ)
					}
				}
			} else if len(t.Shape) == 3 {
				// 3D broadcasting
				for i := 0; i < t.Shape[0]; i++ {
					for j := 0; j < t.Shape[1]; j++ {
						for k := 0; k < t.Shape[2]; k++ {
							otherI, otherJ, otherK := i, j, k
							if other.Shape[0] == 1 {
								otherI = 0
							}
							if other.Shape[1] == 1 {
								otherJ = 0
							}
							if other.Shape[2] == 1 {
								otherK = 0
							}
							idx := i*t.Shape[1]*t.Shape[2] + j*t.Shape[2] + k
							result.Data[idx] -= other.Get(otherI, otherJ, otherK)
						}
					}
				}
			} else {
				panic("broadcasting for Sub only supports 2D and 3D tensors")
			}
			
			return result
		}
	}
	
	panic(fmt.Sprintf("shapes must match or be broadcastable for subtraction: %v and %v", t.Shape, other.Shape))
}

func (t *Tensor) Mul(other *Tensor) *Tensor {
	// Handle same shape
	if shapeEqual(t.Shape, other.Shape) {
		result := t.Copy()
		for i := range result.Data {
			result.Data[i] *= other.Data[i]
		}
		return result
	}
	
	// Handle broadcasting when other is scalar (shape [1])
	if len(other.Shape) == 1 && other.Shape[0] == 1 {
		result := t.Copy()
		scalar := other.Data[0]
		for i := range result.Data {
			result.Data[i] *= scalar
		}
		return result
	}
	
	// Handle broadcasting when other is 1D and t is 2D or 3D (broadcast along last dimension)
	if len(other.Shape) == 1 && other.Shape[0] == t.Shape[len(t.Shape)-1] {
		result := t.Copy()
		
		if len(t.Shape) == 2 {
			// 2D x 1D: (m, n) * (n,)
			for i := 0; i < t.Shape[0]; i++ {
				for j := 0; j < t.Shape[1]; j++ {
					idx := i*t.Shape[1] + j
					result.Data[idx] *= other.Data[j]
				}
			}
		} else if len(t.Shape) == 3 {
			// 3D x 1D: (b, m, n) * (n,)
			for i := 0; i < t.Shape[0]; i++ {
				for j := 0; j < t.Shape[1]; j++ {
					for k := 0; k < t.Shape[2]; k++ {
						idx := i*t.Shape[1]*t.Shape[2] + j*t.Shape[2] + k
						result.Data[idx] *= other.Data[k]
					}
				}
			}
		}
		return result
	}
	
	// Handle broadcasting when other has shape [..., 1]
	if len(t.Shape) == len(other.Shape) {
		canBroadcast := true
		for i := range other.Shape {
			if other.Shape[i] != 1 && other.Shape[i] != t.Shape[i] {
				canBroadcast = false
				break
			}
		}
		
		if canBroadcast {
			result := t.Copy()
			
			if len(t.Shape) == 2 {
				// 2D broadcasting
				for i := 0; i < t.Shape[0]; i++ {
					for j := 0; j < t.Shape[1]; j++ {
						otherI := i
						otherJ := j
						if other.Shape[0] == 1 {
							otherI = 0
						}
						if other.Shape[1] == 1 {
							otherJ = 0
						}
						idx := i*t.Shape[1] + j
						result.Data[idx] *= other.Get(otherI, otherJ)
					}
				}
			} else if len(t.Shape) == 3 {
				// 3D broadcasting
				for i := 0; i < t.Shape[0]; i++ {
					for j := 0; j < t.Shape[1]; j++ {
						for k := 0; k < t.Shape[2]; k++ {
							otherI, otherJ, otherK := i, j, k
							if other.Shape[0] == 1 {
								otherI = 0
							}
							if other.Shape[1] == 1 {
								otherJ = 0
							}
							if other.Shape[2] == 1 {
								otherK = 0
							}
							idx := i*t.Shape[1]*t.Shape[2] + j*t.Shape[2] + k
							result.Data[idx] *= other.Get(otherI, otherJ, otherK)
						}
					}
				}
			} else {
				panic("broadcasting for Mul only supports 2D and 3D tensors")
			}
			
			return result
		}
	}
	
	panic(fmt.Sprintf("shapes must match or be broadcastable for multiplication: %v and %v", t.Shape, other.Shape))
}

func (t *Tensor) Scale(scalar float64) *Tensor {
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] *= scalar
	}
	return result
}

func (t *Tensor) MatMul(other *Tensor) *Tensor {
	// Handle both 2D and 3D (batched) matrix multiplication
	if len(t.Shape) == 2 && len(other.Shape) == 2 {
		// Standard 2D matrix multiplication
		if t.Shape[1] != other.Shape[0] {
			panic(fmt.Sprintf("incompatible shapes for matmul: (%d,%d) and (%d,%d)", 
				t.Shape[0], t.Shape[1], other.Shape[0], other.Shape[1]))
		}
		
		m, k, n := t.Shape[0], t.Shape[1], other.Shape[1]
		result := NewTensor(m, n)
		
		for i := 0; i < m; i++ {
			for j := 0; j < n; j++ {
				sum := 0.0
				for l := 0; l < k; l++ {
					sum += t.Get(i, l) * other.Get(l, j)
				}
				result.Set(sum, i, j)
			}
		}
		
		return result
	} else if len(t.Shape) == 3 && len(other.Shape) == 2 {
		// Batch matrix multiplication: (batch, m, n) x (n, p) -> (batch, m, p)
		if t.Shape[2] != other.Shape[0] {
			panic(fmt.Sprintf("incompatible shapes for batch matmul: (%v) and (%v)", t.Shape, other.Shape))
		}
		
		batchSize := t.Shape[0]
		m := t.Shape[1]
		n := t.Shape[2]
		p := other.Shape[1]
		
		result := NewTensor(batchSize, m, p)
		for b := 0; b < batchSize; b++ {
			for i := 0; i < m; i++ {
				for j := 0; j < p; j++ {
					sum := 0.0
					for k := 0; k < n; k++ {
						sum += t.Get(b, i, k) * other.Get(k, j)
					}
					result.Set(sum, b, i, j)
				}
			}
		}
		return result
	} else if len(t.Shape) == 3 && len(other.Shape) == 3 {
		// Full batch matrix multiplication: (batch, m, n) x (batch, n, p) -> (batch, m, p)
		if t.Shape[0] != other.Shape[0] || t.Shape[2] != other.Shape[1] {
			panic(fmt.Sprintf("incompatible shapes for batch matmul: (%v) and (%v)", t.Shape, other.Shape))
		}
		
		batchSize := t.Shape[0]
		m := t.Shape[1]
		n := t.Shape[2]
		p := other.Shape[2]
		
		result := NewTensor(batchSize, m, p)
		for b := 0; b < batchSize; b++ {
			for i := 0; i < m; i++ {
				for j := 0; j < p; j++ {
					sum := 0.0
					for k := 0; k < n; k++ {
						sum += t.Get(b, i, k) * other.Get(b, k, j)
					}
					result.Set(sum, b, i, j)
				}
			}
		}
		return result
	} else if len(t.Shape) == 4 && len(other.Shape) == 4 {
		// 4D batch matrix multiplication for multi-head attention:
		// (batch, heads, m, n) x (batch, heads, n, p) -> (batch, heads, m, p)
		if t.Shape[0] != other.Shape[0] || t.Shape[1] != other.Shape[1] || t.Shape[3] != other.Shape[2] {
			panic(fmt.Sprintf("incompatible shapes for 4D batch matmul: (%v) and (%v)", t.Shape, other.Shape))
		}
		
		batchSize := t.Shape[0]
		heads := t.Shape[1]
		m := t.Shape[2]
		n := t.Shape[3]
		p := other.Shape[3]
		
		result := NewTensor(batchSize, heads, m, p)
		for b := 0; b < batchSize; b++ {
			for h := 0; h < heads; h++ {
				for i := 0; i < m; i++ {
					for j := 0; j < p; j++ {
						sum := 0.0
						for k := 0; k < n; k++ {
							sum += t.Get(b, h, i, k) * other.Get(b, h, k, j)
						}
						result.Set(sum, b, h, i, j)
					}
				}
			}
		}
		return result
	}
	
	panic(fmt.Sprintf("matmul requires 2D, 3D, or 4D tensors, got shapes %v and %v", t.Shape, other.Shape))
}

func (t *Tensor) Transpose() *Tensor {
	if len(t.Shape) != 2 {
		panic("transpose only supported for 2D tensors")
	}
	
	result := NewTensor(t.Shape[1], t.Shape[0])
	for i := 0; i < t.Shape[0]; i++ {
		for j := 0; j < t.Shape[1]; j++ {
			result.Set(t.Get(i, j), j, i)
		}
	}
	return result
}

func (t *Tensor) Sum() float64 {
	sum := 0.0
	for _, v := range t.Data {
		sum += v
	}
	return sum
}

func (t *Tensor) Mean() float64 {
	return t.Sum() / float64(len(t.Data))
}

func (t *Tensor) Apply(fn func(float64) float64) *Tensor {
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] = fn(result.Data[i])
	}
	return result
}

func RandomTensor(shape ...int) *Tensor {
	t := NewTensor(shape...)
	for i := range t.Data {
		t.Data[i] = rand.NormFloat64()
	}
	return t
}

func XavierInit(shape ...int) *Tensor {
	t := NewTensor(shape...)
	// For weight matrices (inputSize, outputSize), use fanIn = inputSize
	fanIn := float64(shape[0])
	
	// Xavier/Glorot initialization: sqrt(2 / (fanIn + fanOut))
	// For ReLU networks, He initialization is better: sqrt(2 / fanIn)
	scale := math.Sqrt(2.0 / fanIn)  // Using He initialization for ReLU
	for i := range t.Data {
		t.Data[i] = rand.NormFloat64() * scale
	}
	return t
}

func shapeEqual(a, b []int) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// Helper functions for tensor operations

func Zeros(shape []int) *Tensor {
	return NewTensor(shape...)
}

func Ones(shape []int) *Tensor {
	t := NewTensor(shape...)
	for i := range t.Data {
		t.Data[i] = 1.0
	}
	return t
}

func Reshape(t *Tensor, shape []int) *Tensor {
	return t.Reshape(shape...)
}

func Transpose(t *Tensor, axes []int) *Tensor {
	// For now, just handle 2D transpose
	if len(axes) == 2 && axes[0] == 1 && axes[1] == 0 {
		return t.Transpose()
	}
	// For 4D transpose, use TransposeAxes if available
	if len(t.Shape) == 4 && len(axes) == 4 {
		return t.TransposeAxes(axes[0], axes[1], axes[2], axes[3])
	}
	panic("Transpose with custom axes not fully implemented")
}

func MatMul(a, b *Tensor) *Tensor {
	return a.MatMul(b)
}

func Scale(t *Tensor, scalar float64) *Tensor {
	return t.Scale(scalar)
}

func Add(a, b *Tensor) *Tensor {
	return a.Add(b)
}

func Multiply(a, b *Tensor) *Tensor {
	return a.Mul(b)
}

func XavierUniform(shape []int) *Tensor {
	return XavierInit(shape...)
}

func AddBias(t *Tensor, bias *Tensor) *Tensor {
	// Broadcast bias across batch dimension
	result := t.Copy()
	
	if len(t.Shape) == 2 {
		// 2D case: (m, n) + (n,) -> (m, n)
		for i := 0; i < t.Shape[0]; i++ {
			for j := 0; j < t.Shape[1]; j++ {
				result.Data[i*t.Shape[1]+j] += bias.Data[j]
			}
		}
	} else if len(t.Shape) == 3 {
		// 3D case: (batch, m, n) + (n,) -> (batch, m, n)
		batchSize := t.Shape[0]
		m := t.Shape[1]
		n := t.Shape[2]
		
		for b := 0; b < batchSize; b++ {
			for i := 0; i < m; i++ {
				for j := 0; j < n; j++ {
					idx := b*m*n + i*n + j
					result.Data[idx] += bias.Data[j]
				}
			}
		}
	} else {
		panic("AddBias only supports 2D and 3D tensors")
	}
	
	return result
}

func AddScalar(t *Tensor, scalar float64) *Tensor {
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] += scalar
	}
	return result
}

func Divide(a, b *Tensor) *Tensor {
	// Handle same shape
	if shapeEqual(a.Shape, b.Shape) {
		result := a.Copy()
		for i := range result.Data {
			result.Data[i] /= b.Data[i]
		}
		return result
	}
	
	// Handle broadcasting when b has shape [..., 1]
	if len(a.Shape) == len(b.Shape) {
		canBroadcast := true
		for i := range b.Shape {
			if b.Shape[i] != 1 && b.Shape[i] != a.Shape[i] {
				canBroadcast = false
				break
			}
		}
		
		if canBroadcast {
			result := a.Copy()
			
			if len(a.Shape) == 2 {
				// 2D broadcasting
				for i := 0; i < a.Shape[0]; i++ {
					for j := 0; j < a.Shape[1]; j++ {
						otherI := i
						otherJ := j
						if b.Shape[0] == 1 {
							otherI = 0
						}
						if b.Shape[1] == 1 {
							otherJ = 0
						}
						idx := i*a.Shape[1] + j
						result.Data[idx] /= b.Get(otherI, otherJ)
					}
				}
			} else if len(a.Shape) == 3 {
				// 3D broadcasting
				for i := 0; i < a.Shape[0]; i++ {
					for j := 0; j < a.Shape[1]; j++ {
						for k := 0; k < a.Shape[2]; k++ {
							otherI, otherJ, otherK := i, j, k
							if b.Shape[0] == 1 {
								otherI = 0
							}
							if b.Shape[1] == 1 {
								otherJ = 0
							}
							if b.Shape[2] == 1 {
								otherK = 0
							}
							idx := i*a.Shape[1]*a.Shape[2] + j*a.Shape[2] + k
							result.Data[idx] /= b.Get(otherI, otherJ, otherK)
						}
					}
				}
			} else {
				panic("broadcasting for Divide only supports 2D and 3D tensors")
			}
			
			return result
		}
	}
	
	panic(fmt.Sprintf("shapes must match or be broadcastable for division: %v and %v", a.Shape, b.Shape))
}

func Subtract(a, b *Tensor) *Tensor {
	return a.Sub(b)
}

func Slice(t *Tensor, start, end int) *Tensor {
	// Simple slice along first dimension
	if len(t.Shape) == 1 {
		sliceLen := end - start
		result := NewTensor(sliceLen)
		copy(result.Data, t.Data[start:end])
		return result
	} else if len(t.Shape) == 2 {
		// Slice rows of a 2D tensor
		rows := end - start
		cols := t.Shape[1]
		result := NewTensor(rows, cols)
		for i := 0; i < rows; i++ {
			for j := 0; j < cols; j++ {
				result.Set(t.Get(start+i, j), i, j)
			}
		}
		return result
	}
	panic("Slice only supports 1D and 2D tensors currently")
}

func SliceAlongDim(t *Tensor, start, end, dim int) *Tensor {
	// Slice along a specific dimension
	if dim != 0 {
		panic("SliceAlongDim only supports dim=0 currently")
	}
	return Slice(t, start, end)
}

func Unsqueeze(t *Tensor, dim int) *Tensor {
	// Add a dimension of size 1 at the specified position
	newShape := make([]int, len(t.Shape)+1)
	for i := 0; i < dim; i++ {
		newShape[i] = t.Shape[i]
	}
	newShape[dim] = 1
	for i := dim; i < len(t.Shape); i++ {
		newShape[i+1] = t.Shape[i]
	}
	return t.Reshape(newShape...)
}

func RandomBernoulli(shape []int, p float64) *Tensor {
	t := NewTensor(shape...)
	for i := range t.Data {
		if rand.Float64() < p {
			t.Data[i] = 1.0
		} else {
			t.Data[i] = 0.0
		}
	}
	return t
}

func SqrtTensor(t *Tensor) *Tensor {
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] = math.Sqrt(result.Data[i])
	}
	return result
}

func DivideScalar(t *Tensor, scalar float64) *Tensor {
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] /= scalar
	}
	return result
}

func RandomNormal(shape []int, mean, std float64) *Tensor {
	t := NewTensor(shape...)
	for i := range t.Data {
		t.Data[i] = rand.NormFloat64()*std + mean
	}
	return t
}

func Squeeze(t *Tensor, dim int) *Tensor {
	// Remove a dimension of size 1 at the specified position
	if dim < 0 || dim >= len(t.Shape) {
		panic("Invalid dimension for squeeze")
	}
	if t.Shape[dim] != 1 {
		panic("Can only squeeze dimensions of size 1")
	}
	
	newShape := make([]int, len(t.Shape)-1)
	j := 0
	for i := 0; i < len(t.Shape); i++ {
		if i != dim {
			newShape[j] = t.Shape[i]
			j++
		}
	}
	return t.Reshape(newShape...)
}