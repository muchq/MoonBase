package activations

import (
	"math"
	
	"github.com/MoonBase/go/neuro/utils"
)

type Activation interface {
	Forward(x *utils.Tensor) *utils.Tensor
	Backward(grad *utils.Tensor, cache *utils.Tensor) *utils.Tensor
	Name() string
}

type ReLU struct{}

func NewReLU() *ReLU {
	return &ReLU{}
}

func (r *ReLU) Forward(x *utils.Tensor) *utils.Tensor {
	return x.Apply(func(v float64) float64 {
		if v > 0 {
			return v
		}
		return 0
	})
}

func (r *ReLU) Backward(grad *utils.Tensor, cache *utils.Tensor) *utils.Tensor {
	result := grad.Copy()
	for i := range result.Data {
		if cache.Data[i] <= 0 {
			result.Data[i] = 0
		}
	}
	return result
}

func (r *ReLU) Name() string {
	return "ReLU"
}

type Sigmoid struct{}

func NewSigmoid() *Sigmoid {
	return &Sigmoid{}
}

func (s *Sigmoid) Forward(x *utils.Tensor) *utils.Tensor {
	return x.Apply(func(v float64) float64 {
		return 1.0 / (1.0 + math.Exp(-v))
	})
}

func (s *Sigmoid) Backward(grad *utils.Tensor, cache *utils.Tensor) *utils.Tensor {
	result := grad.Copy()
	for i := range result.Data {
		sig := cache.Data[i]  // cache already contains sigmoid output
		result.Data[i] *= sig * (1 - sig)
	}
	return result
}

func (s *Sigmoid) Name() string {
	return "Sigmoid"
}

type Tanh struct{}

func NewTanh() *Tanh {
	return &Tanh{}
}

func (t *Tanh) Forward(x *utils.Tensor) *utils.Tensor {
	return x.Apply(math.Tanh)
}

func (t *Tanh) Backward(grad *utils.Tensor, cache *utils.Tensor) *utils.Tensor {
	result := grad.Copy()
	for i := range result.Data {
		th := cache.Data[i]  // cache already contains tanh output
		result.Data[i] *= (1 - th*th)
	}
	return result
}

func (t *Tanh) Name() string {
	return "Tanh"
}

type Softmax struct{}

func NewSoftmax() *Softmax {
	return &Softmax{}
}

func (s *Softmax) Forward(x *utils.Tensor) *utils.Tensor {
	result := x.Copy()
	
	if len(x.Shape) == 1 {
		s.softmax1D(result)
	} else if len(x.Shape) == 2 {
		for i := 0; i < x.Shape[0]; i++ {
			s.softmaxRow(result, i)
		}
	} else {
		panic("Softmax only supports 1D and 2D tensors")
	}
	
	return result
}

func (s *Softmax) softmax1D(t *utils.Tensor) {
	max := t.Data[0]
	for _, v := range t.Data {
		if v > max {
			max = v
		}
	}
	
	sum := 0.0
	for i := range t.Data {
		t.Data[i] = math.Exp(t.Data[i] - max)
		sum += t.Data[i]
	}
	
	for i := range t.Data {
		t.Data[i] /= sum
	}
}

func (s *Softmax) softmaxRow(t *utils.Tensor, row int) {
	start := row * t.Shape[1]
	end := start + t.Shape[1]
	
	max := t.Data[start]
	for i := start; i < end; i++ {
		if t.Data[i] > max {
			max = t.Data[i]
		}
	}
	
	sum := 0.0
	for i := start; i < end; i++ {
		t.Data[i] = math.Exp(t.Data[i] - max)
		sum += t.Data[i]
	}
	
	for i := start; i < end; i++ {
		t.Data[i] /= sum
	}
}

func (s *Softmax) Backward(grad *utils.Tensor, cache *utils.Tensor) *utils.Tensor {
	softmax := s.Forward(cache)
	result := utils.NewTensor(grad.Shape...)
	
	if len(grad.Shape) == 1 {
		for i := range result.Data {
			for j := range result.Data {
				if i == j {
					result.Data[i] += grad.Data[j] * softmax.Data[i] * (1 - softmax.Data[i])
				} else {
					result.Data[i] -= grad.Data[j] * softmax.Data[i] * softmax.Data[j]
				}
			}
		}
	} else if len(grad.Shape) == 2 {
		for b := 0; b < grad.Shape[0]; b++ {
			for i := 0; i < grad.Shape[1]; i++ {
				for j := 0; j < grad.Shape[1]; j++ {
					idx_i := b*grad.Shape[1] + i
					idx_j := b*grad.Shape[1] + j
					if i == j {
						result.Data[idx_i] += grad.Data[idx_j] * softmax.Data[idx_i] * (1 - softmax.Data[idx_i])
					} else {
						result.Data[idx_i] -= grad.Data[idx_j] * softmax.Data[idx_i] * softmax.Data[idx_j]
					}
				}
			}
		}
	}
	
	return result
}

func (s *Softmax) Name() string {
	return "Softmax"
}