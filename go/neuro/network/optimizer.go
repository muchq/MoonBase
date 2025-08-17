package network

import (
	"math"
	
	"github.com/muchq/moonbase/go/neuro/layers"
	"github.com/muchq/moonbase/go/neuro/utils"
	"gonum.org/v1/gonum/floats"
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
	
	// Update weights using vectorized operations
	if layer.GradW != nil {
		// m = beta1 * m + (1 - beta1) * grad
		floats.Scale(a.beta1, mW.Data)
		floats.AddScaled(mW.Data, 1-a.beta1, layer.GradW.Data)
		
		// v = beta2 * v + (1 - beta2) * grad^2
		gradSquared := make([]float64, len(layer.GradW.Data))
		copy(gradSquared, layer.GradW.Data)
		floats.Mul(gradSquared, layer.GradW.Data) // grad^2
		
		floats.Scale(a.beta2, vW.Data)
		floats.AddScaled(vW.Data, 1-a.beta2, gradSquared)
		
		// Bias correction and update
		mHatCorr := 1.0 / (1 - math.Pow(a.beta1, float64(a.t)))
		vHatCorr := 1.0 / (1 - math.Pow(a.beta2, float64(a.t)))
		
		// weights = weights - lr * m_hat / (sqrt(v_hat) + eps)
		for i := range weights.Data {
			mHat := mW.Data[i] * mHatCorr
			vHat := vW.Data[i] * vHatCorr
			weights.Data[i] -= a.lr * mHat / (math.Sqrt(vHat) + a.epsilon)
		}
	}
	
	// Update bias using vectorized operations
	if layer.GradB != nil {
		// m = beta1 * m + (1 - beta1) * grad
		floats.Scale(a.beta1, mB.Data)
		floats.AddScaled(mB.Data, 1-a.beta1, layer.GradB.Data)
		
		// v = beta2 * v + (1 - beta2) * grad^2
		gradSquared := make([]float64, len(layer.GradB.Data))
		copy(gradSquared, layer.GradB.Data)
		floats.Mul(gradSquared, layer.GradB.Data) // grad^2
		
		floats.Scale(a.beta2, vB.Data)
		floats.AddScaled(vB.Data, 1-a.beta2, gradSquared)
		
		// Bias correction and update
		mHatCorr := 1.0 / (1 - math.Pow(a.beta1, float64(a.t)))
		vHatCorr := 1.0 / (1 - math.Pow(a.beta2, float64(a.t)))
		
		// bias = bias - lr * m_hat / (sqrt(v_hat) + eps)
		for i := range bias.Data {
			mHat := mB.Data[i] * mHatCorr
			vHat := vB.Data[i] * vHatCorr
			bias.Data[i] -= a.lr * mHat / (math.Sqrt(vHat) + a.epsilon)
		}
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
	
	// Update gamma using vectorized operations
	if layer.GradGamma != nil {
		// m = beta1 * m + (1 - beta1) * grad
		floats.Scale(a.beta1, mGamma.Data)
		floats.AddScaled(mGamma.Data, 1-a.beta1, layer.GradGamma.Data)
		
		// v = beta2 * v + (1 - beta2) * grad^2
		gradSquared := make([]float64, len(layer.GradGamma.Data))
		copy(gradSquared, layer.GradGamma.Data)
		floats.Mul(gradSquared, layer.GradGamma.Data) // grad^2
		
		floats.Scale(a.beta2, vGamma.Data)
		floats.AddScaled(vGamma.Data, 1-a.beta2, gradSquared)
		
		// Bias correction and update
		mHatCorr := 1.0 / (1 - math.Pow(a.beta1, float64(a.t)))
		vHatCorr := 1.0 / (1 - math.Pow(a.beta2, float64(a.t)))
		
		for i := range gamma.Data {
			mHat := mGamma.Data[i] * mHatCorr
			vHat := vGamma.Data[i] * vHatCorr
			gamma.Data[i] -= a.lr * mHat / (math.Sqrt(vHat) + a.epsilon)
		}
	}
	
	// Update beta using vectorized operations
	if layer.GradBeta != nil {
		// m = beta1 * m + (1 - beta1) * grad
		floats.Scale(a.beta1, mBeta.Data)
		floats.AddScaled(mBeta.Data, 1-a.beta1, layer.GradBeta.Data)
		
		// v = beta2 * v + (1 - beta2) * grad^2
		gradSquared := make([]float64, len(layer.GradBeta.Data))
		copy(gradSquared, layer.GradBeta.Data)
		floats.Mul(gradSquared, layer.GradBeta.Data) // grad^2
		
		floats.Scale(a.beta2, vBeta.Data)
		floats.AddScaled(vBeta.Data, 1-a.beta2, gradSquared)
		
		// Bias correction and update
		mHatCorr := 1.0 / (1 - math.Pow(a.beta1, float64(a.t)))
		vHatCorr := 1.0 / (1 - math.Pow(a.beta2, float64(a.t)))
		
		for i := range beta.Data {
			mHat := mBeta.Data[i] * mHatCorr
			vHat := vBeta.Data[i] * vHatCorr
			beta.Data[i] -= a.lr * mHat / (math.Sqrt(vHat) + a.epsilon)
		}
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