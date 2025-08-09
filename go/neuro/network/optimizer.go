package network

import (
	"math"
	
	"github.com/MoonBase/go/neuro/layers"
	"github.com/MoonBase/go/neuro/utils"
)

type Optimizer interface {
	Step()
	SetLayers([]layers.Layer)
	Name() string
}

type SGD struct {
	lr       float64
	momentum float64
	layers   []layers.Layer
	velocity map[layers.Layer][]*utils.Tensor
}

func NewSGD(lr, momentum float64) *SGD {
	return &SGD{
		lr:       lr,
		momentum: momentum,
		velocity: make(map[layers.Layer][]*utils.Tensor),
	}
}

func (s *SGD) SetLayers(layers []layers.Layer) {
	s.layers = layers
	for _, layer := range layers {
		params := layer.GetParams()
		velocities := make([]*utils.Tensor, len(params))
		for i, param := range params {
			velocities[i] = utils.NewTensor(param.Shape...)
		}
		s.velocity[layer] = velocities
	}
}

func (s *SGD) Step() {
	for _, layer := range s.layers {
		layer.UpdateWeights(s.lr)
	}
}

func (s *SGD) Name() string {
	return "SGD"
}

type Adam struct {
	lr      float64
	beta1   float64
	beta2   float64
	epsilon float64
	t       int
	layers  []layers.Layer
	m       map[layers.Layer][]*utils.Tensor
	v       map[layers.Layer][]*utils.Tensor
}

func NewAdam(lr float64) *Adam {
	return &Adam{
		lr:      lr,
		beta1:   0.9,
		beta2:   0.999,
		epsilon: 1e-8,
		t:       0,
		m:       make(map[layers.Layer][]*utils.Tensor),
		v:       make(map[layers.Layer][]*utils.Tensor),
	}
}

func (a *Adam) SetLayers(layers []layers.Layer) {
	a.layers = layers
	for _, layer := range layers {
		params := layer.GetParams()
		ms := make([]*utils.Tensor, len(params))
		vs := make([]*utils.Tensor, len(params))
		for i, param := range params {
			ms[i] = utils.NewTensor(param.Shape...)
			vs[i] = utils.NewTensor(param.Shape...)
		}
		a.m[layer] = ms
		a.v[layer] = vs
	}
}

func (a *Adam) Step() {
	a.t++
	
	for _, layer := range a.layers {
		switch l := layer.(type) {
		case *layers.Dense:
			a.updateDense(l)
		case *layers.BatchNorm:
			a.updateBatchNorm(l)
		}
	}
}

func (a *Adam) updateDense(layer *layers.Dense) {
	params := layer.GetParams()
	if len(params) < 2 {
		return
	}
	
	weights := params[0]
	bias := params[1]
	
	mW := a.m[layer][0]
	mB := a.m[layer][1]
	vW := a.v[layer][0]
	vB := a.v[layer][1]
	
	for i := range weights.Data {
		grad := 0.0
		if layer.GradW != nil {
			grad = layer.GradW.Data[i]
		}
		
		mW.Data[i] = a.beta1*mW.Data[i] + (1-a.beta1)*grad
		vW.Data[i] = a.beta2*vW.Data[i] + (1-a.beta2)*grad*grad
		
		mHat := mW.Data[i] / (1 - math.Pow(a.beta1, float64(a.t)))
		vHat := vW.Data[i] / (1 - math.Pow(a.beta2, float64(a.t)))
		
		weights.Data[i] -= a.lr * mHat / (math.Sqrt(vHat) + a.epsilon)
	}
	
	for i := range bias.Data {
		grad := 0.0
		if layer.GradB != nil {
			grad = layer.GradB.Data[i]
		}
		
		mB.Data[i] = a.beta1*mB.Data[i] + (1-a.beta1)*grad
		vB.Data[i] = a.beta2*vB.Data[i] + (1-a.beta2)*grad*grad
		
		mHat := mB.Data[i] / (1 - math.Pow(a.beta1, float64(a.t)))
		vHat := vB.Data[i] / (1 - math.Pow(a.beta2, float64(a.t)))
		
		bias.Data[i] -= a.lr * mHat / (math.Sqrt(vHat) + a.epsilon)
	}
}

func (a *Adam) updateBatchNorm(layer *layers.BatchNorm) {
	params := layer.GetParams()
	if len(params) < 2 {
		return
	}
	
	gamma := params[0]
	beta := params[1]
	
	mGamma := a.m[layer][0]
	mBeta := a.m[layer][1]
	vGamma := a.v[layer][0]
	vBeta := a.v[layer][1]
	
	for i := range gamma.Data {
		grad := 0.0
		if layer.GradGamma != nil {
			grad = layer.GradGamma.Data[i]
		}
		
		mGamma.Data[i] = a.beta1*mGamma.Data[i] + (1-a.beta1)*grad
		vGamma.Data[i] = a.beta2*vGamma.Data[i] + (1-a.beta2)*grad*grad
		
		mHat := mGamma.Data[i] / (1 - math.Pow(a.beta1, float64(a.t)))
		vHat := vGamma.Data[i] / (1 - math.Pow(a.beta2, float64(a.t)))
		
		gamma.Data[i] -= a.lr * mHat / (math.Sqrt(vHat) + a.epsilon)
	}
	
	for i := range beta.Data {
		grad := 0.0
		if layer.GradBeta != nil {
			grad = layer.GradBeta.Data[i]
		}
		
		mBeta.Data[i] = a.beta1*mBeta.Data[i] + (1-a.beta1)*grad
		vBeta.Data[i] = a.beta2*vBeta.Data[i] + (1-a.beta2)*grad*grad
		
		mHat := mBeta.Data[i] / (1 - math.Pow(a.beta1, float64(a.t)))
		vHat := vBeta.Data[i] / (1 - math.Pow(a.beta2, float64(a.t)))
		
		beta.Data[i] -= a.lr * mHat / (math.Sqrt(vHat) + a.epsilon)
	}
}

func (a *Adam) Name() string {
	return "Adam"
}

type RMSprop struct {
	lr      float64
	decay   float64
	epsilon float64
	layers  []layers.Layer
	cache   map[layers.Layer][]*utils.Tensor
}

func NewRMSprop(lr float64) *RMSprop {
	return &RMSprop{
		lr:      lr,
		decay:   0.9,
		epsilon: 1e-8,
		cache:   make(map[layers.Layer][]*utils.Tensor),
	}
}

func (r *RMSprop) SetLayers(layers []layers.Layer) {
	r.layers = layers
	for _, layer := range layers {
		params := layer.GetParams()
		caches := make([]*utils.Tensor, len(params))
		for i, param := range params {
			caches[i] = utils.NewTensor(param.Shape...)
		}
		r.cache[layer] = caches
	}
}

func (r *RMSprop) Step() {
	for _, layer := range r.layers {
		layer.UpdateWeights(r.lr)
	}
}

func (r *RMSprop) Name() string {
	return "RMSprop"
}