package utils

import (
	"fmt"
	"math"
	"math/rand"
	"gonum.org/v1/gonum/mat"
	"gonum.org/v1/gonum/floats"
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
	// Handle same shape - use gonum for efficient vector addition
	if shapeEqual(t.Shape, other.Shape) {
		result := t.Copy()
		floats.Add(result.Data, other.Data)
		return result
	}
	
	// Handle broadcasting when other is scalar (shape [1]) - use gonum
	if len(other.Shape) == 1 && other.Shape[0] == 1 {
		result := t.Copy()
		floats.AddConst(other.Data[0], result.Data)
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
		// Same shape - element-wise subtraction using gonum
		result := t.Copy()
		floats.Sub(result.Data, other.Data)
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
	// Handle same shape - use gonum for efficient element-wise multiplication
	if shapeEqual(t.Shape, other.Shape) {
		result := t.Copy()
		floats.Mul(result.Data, other.Data)
		return result
	}
	
	// Handle broadcasting when other is scalar (shape [1]) - use gonum
	if len(other.Shape) == 1 && other.Shape[0] == 1 {
		result := t.Copy()
		floats.Scale(other.Data[0], result.Data)
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
	floats.Scale(scalar, result.Data)
	return result
}

func (t *Tensor) MatMul(other *Tensor) *Tensor {
	// Handle both 2D and 3D (batched) matrix multiplication
	if len(t.Shape) == 2 && len(other.Shape) == 2 {
		// Standard 2D matrix multiplication using Gonum
		if t.Shape[1] != other.Shape[0] {
			panic(fmt.Sprintf("incompatible shapes for matmul: (%d,%d) and (%d,%d)", 
				t.Shape[0], t.Shape[1], other.Shape[0], other.Shape[1]))
		}
		
		m, k, n := t.Shape[0], t.Shape[1], other.Shape[1]
		
		// Use Gonum for optimized matrix multiplication
		a := mat.NewDense(m, k, t.Data)
		b := mat.NewDense(k, n, other.Data)
		c := mat.NewDense(m, n, nil)
		c.Mul(a, b)
		
		// Extract the result data
		result := NewTensor(m, n)
		copy(result.Data, c.RawMatrix().Data)
		
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
		
		// Use Gonum for each batch
		b_mat := mat.NewDense(n, p, other.Data)
		
		for b := 0; b < batchSize; b++ {
			// Extract batch slice
			batchStart := b * m * n
			a_mat := mat.NewDense(m, n, t.Data[batchStart:batchStart+m*n])
			c_mat := mat.NewDense(m, p, nil)
			c_mat.Mul(a_mat, b_mat)
			
			// Copy result to output tensor
			resultStart := b * m * p
			copy(result.Data[resultStart:resultStart+m*p], c_mat.RawMatrix().Data)
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
		
		// Use Gonum for each batch
		for b := 0; b < batchSize; b++ {
			// Extract batch slices
			aStart := b * m * n
			bStart := b * n * p
			a_mat := mat.NewDense(m, n, t.Data[aStart:aStart+m*n])
			b_mat := mat.NewDense(n, p, other.Data[bStart:bStart+n*p])
			c_mat := mat.NewDense(m, p, nil)
			c_mat.Mul(a_mat, b_mat)
			
			// Copy result to output tensor
			resultStart := b * m * p
			copy(result.Data[resultStart:resultStart+m*p], c_mat.RawMatrix().Data)
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
		
		// Use Gonum for each batch and head
		for b := 0; b < batchSize; b++ {
			for h := 0; h < heads; h++ {
				// Calculate offsets for this batch and head
				aOffset := (b*heads + h) * m * n
				bOffset := (b*heads + h) * n * p
				
				// Create matrices for this batch/head
				a_mat := mat.NewDense(m, n, t.Data[aOffset:aOffset+m*n])
				b_mat := mat.NewDense(n, p, other.Data[bOffset:bOffset+n*p])
				c_mat := mat.NewDense(m, p, nil)
				c_mat.Mul(a_mat, b_mat)
				
				// Copy result to output tensor
				resultOffset := (b*heads + h) * m * p
				copy(result.Data[resultOffset:resultOffset+m*p], c_mat.RawMatrix().Data)
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
	
	// Use Gonum for efficient transpose
	m := mat.NewDense(t.Shape[0], t.Shape[1], t.Data)
	transposed := mat.DenseCopyOf(m.T())
	
	result := NewTensor(t.Shape[1], t.Shape[0])
	copy(result.Data, transposed.RawMatrix().Data)
	
	return result
}

func (t *Tensor) Sum() float64 {
	return floats.Sum(t.Data)
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
	floats.AddConst(scalar, result.Data)
	return result
}

func Divide(a, b *Tensor) *Tensor {
	// Handle same shape - use gonum for element-wise division
	if shapeEqual(a.Shape, b.Shape) {
		result := a.Copy()
		floats.Div(result.Data, b.Data)
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
	floats.Scale(1.0/scalar, result.Data)
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