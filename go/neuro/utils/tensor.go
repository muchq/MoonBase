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
	if !shapeEqual(t.Shape, other.Shape) {
		panic("shapes must match for addition")
	}
	
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] += other.Data[i]
	}
	return result
}

func (t *Tensor) Sub(other *Tensor) *Tensor {
	if !shapeEqual(t.Shape, other.Shape) {
		panic("shapes must match for subtraction")
	}
	
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] -= other.Data[i]
	}
	return result
}

func (t *Tensor) Mul(other *Tensor) *Tensor {
	if !shapeEqual(t.Shape, other.Shape) {
		panic("shapes must match for element-wise multiplication")
	}
	
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] *= other.Data[i]
	}
	return result
}

func (t *Tensor) Scale(scalar float64) *Tensor {
	result := t.Copy()
	for i := range result.Data {
		result.Data[i] *= scalar
	}
	return result
}

func (t *Tensor) MatMul(other *Tensor) *Tensor {
	if len(t.Shape) != 2 || len(other.Shape) != 2 {
		panic("matmul requires 2D tensors")
	}
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