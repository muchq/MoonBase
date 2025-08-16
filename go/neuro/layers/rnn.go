package layers

import (
	"fmt"
	"math"
	
	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/utils"
)

type RNN struct {
	InputSize  int
	HiddenSize int
	
	WeightIH   *utils.Tensor
	WeightHH   *utils.Tensor
	BiasIH     *utils.Tensor
	BiasHH     *utils.Tensor
	
	Activation activations.Activation
	
	HiddenState *utils.Tensor
	
	InputCache  []*utils.Tensor
	HiddenCache []*utils.Tensor
	OutputCache []*utils.Tensor
	
	GradWeightIH *utils.Tensor
	GradWeightHH *utils.Tensor
	GradBiasIH   *utils.Tensor
	GradBiasHH   *utils.Tensor
	
	GradientClipValue float64
}

func NewRNN(inputSize, hiddenSize int, activation activations.Activation) *RNN {
	if activation == nil {
		activation = activations.NewTanh()
	}
	
	weightIH := utils.XavierInit(inputSize, hiddenSize)
	weightHH := utils.XavierInit(hiddenSize, hiddenSize)
	
	scale := 1.0 / math.Sqrt(float64(hiddenSize))
	for i := range weightHH.Data {
		weightHH.Data[i] *= scale
	}
	
	return &RNN{
		InputSize:         inputSize,
		HiddenSize:        hiddenSize,
		WeightIH:          weightIH,
		WeightHH:          weightHH,
		BiasIH:            utils.NewTensor(hiddenSize),
		BiasHH:            utils.NewTensor(hiddenSize),
		Activation:        activation,
		GradientClipValue: 5.0,
	}
}

func (r *RNN) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	var batchSize, seqLen int
	
	if len(input.Shape) == 3 {
		batchSize = input.Shape[0]
		seqLen = input.Shape[1]
		if input.Shape[2] != r.InputSize {
			panic(fmt.Sprintf("Expected input size %d, got %d", r.InputSize, input.Shape[2]))
		}
	} else if len(input.Shape) == 2 {
		batchSize = 1
		seqLen = input.Shape[0]
		if input.Shape[1] != r.InputSize {
			panic(fmt.Sprintf("Expected input size %d, got %d", r.InputSize, input.Shape[1]))
		}
		input = input.Reshape(1, seqLen, r.InputSize)
	} else {
		panic("RNN expects 2D (seq_len, input_size) or 3D (batch, seq_len, input_size) input")
	}
	
	if training {
		r.InputCache = make([]*utils.Tensor, seqLen)
		r.HiddenCache = make([]*utils.Tensor, seqLen+1)
		r.OutputCache = make([]*utils.Tensor, seqLen)
	}
	
	if r.HiddenState == nil || r.HiddenState.Shape[0] != batchSize {
		r.HiddenState = utils.NewTensor(batchSize, r.HiddenSize)
	}
	
	if training {
		r.HiddenCache[0] = r.HiddenState.Copy()
	}
	
	outputs := make([]*utils.Tensor, seqLen)
	
	for t := 0; t < seqLen; t++ {
		xt := r.getTimeStep(input, t)
		
		if training {
			r.InputCache[t] = xt.Copy()
		}
		
		ih := xt.MatMul(r.WeightIH)
		for b := 0; b < batchSize; b++ {
			for h := 0; h < r.HiddenSize; h++ {
				idx := b*r.HiddenSize + h
				ih.Data[idx] += r.BiasIH.Data[h]
			}
		}
		
		hh := r.HiddenState.MatMul(r.WeightHH)
		for b := 0; b < batchSize; b++ {
			for h := 0; h < r.HiddenSize; h++ {
				idx := b*r.HiddenSize + h
				hh.Data[idx] += r.BiasHH.Data[h]
			}
		}
		
		combined := ih.Add(hh)
		r.HiddenState = r.Activation.Forward(combined)
		
		if training {
			r.HiddenCache[t+1] = r.HiddenState.Copy()
			r.OutputCache[t] = combined.Copy()
		}
		
		outputs[t] = r.HiddenState.Copy()
	}
	
	output := utils.NewTensor(batchSize, seqLen, r.HiddenSize)
	for t := 0; t < seqLen; t++ {
		for b := 0; b < batchSize; b++ {
			for h := 0; h < r.HiddenSize; h++ {
				srcIdx := b*r.HiddenSize + h
				dstIdx := b*seqLen*r.HiddenSize + t*r.HiddenSize + h
				output.Data[dstIdx] = outputs[t].Data[srcIdx]
			}
		}
	}
	
	return output
}

func (r *RNN) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	batchSize := gradOutput.Shape[0]
	seqLen := gradOutput.Shape[1]
	
	r.GradWeightIH = utils.NewTensor(r.InputSize, r.HiddenSize)
	r.GradWeightHH = utils.NewTensor(r.HiddenSize, r.HiddenSize)
	r.GradBiasIH = utils.NewTensor(r.HiddenSize)
	r.GradBiasHH = utils.NewTensor(r.HiddenSize)
	
	gradInput := utils.NewTensor(batchSize, seqLen, r.InputSize)
	gradHidden := utils.NewTensor(batchSize, r.HiddenSize)
	
	for t := seqLen - 1; t >= 0; t-- {
		gradT := r.getTimeStep(gradOutput, t)
		
		gradTotal := gradT.Add(gradHidden)
		
		gradActivation := r.Activation.Backward(gradTotal, r.HiddenCache[t+1])
		
		r.clipGradient(gradActivation)
		
		gradIH := r.InputCache[t].Transpose().MatMul(gradActivation)
		for i := range r.GradWeightIH.Data {
			r.GradWeightIH.Data[i] += gradIH.Data[i]
		}
		
		gradHH := r.HiddenCache[t].Transpose().MatMul(gradActivation)
		for i := range r.GradWeightHH.Data {
			r.GradWeightHH.Data[i] += gradHH.Data[i]
		}
		
		for b := 0; b < batchSize; b++ {
			for h := 0; h < r.HiddenSize; h++ {
				idx := b*r.HiddenSize + h
				r.GradBiasIH.Data[h] += gradActivation.Data[idx]
				r.GradBiasHH.Data[h] += gradActivation.Data[idx]
			}
		}
		
		gradInputT := gradActivation.MatMul(r.WeightIH.Transpose())
		r.setTimeStep(gradInput, t, gradInputT)
		
		gradHidden = gradActivation.MatMul(r.WeightHH.Transpose())
	}
	
	return gradInput
}

func (r *RNN) UpdateWeights(lr float64) {
	if r.GradWeightIH == nil {
		return
	}
	
	for i := range r.WeightIH.Data {
		r.WeightIH.Data[i] -= lr * r.GradWeightIH.Data[i]
	}
	for i := range r.WeightHH.Data {
		r.WeightHH.Data[i] -= lr * r.GradWeightHH.Data[i]
	}
	for i := range r.BiasIH.Data {
		r.BiasIH.Data[i] -= lr * r.GradBiasIH.Data[i]
	}
	for i := range r.BiasHH.Data {
		r.BiasHH.Data[i] -= lr * r.GradBiasHH.Data[i]
	}
}

func (r *RNN) GetParams() []*utils.Tensor {
	return []*utils.Tensor{r.WeightIH, r.WeightHH, r.BiasIH, r.BiasHH}
}

func (r *RNN) GetGradients() []*utils.Tensor {
	return []*utils.Tensor{r.GradWeightIH, r.GradWeightHH, r.GradBiasIH, r.GradBiasHH}
}

func (r *RNN) SetParams(params []*utils.Tensor) {
	if len(params) != 4 {
		panic("RNN layer expects 4 parameter tensors")
	}
	r.WeightIH = params[0]
	r.WeightHH = params[1]
	r.BiasIH = params[2]
	r.BiasHH = params[3]
}

func (r *RNN) Name() string {
	activationName := "Tanh"
	if r.Activation != nil {
		activationName = r.Activation.Name()
	}
	return fmt.Sprintf("RNN(%d, %d, %s)", r.InputSize, r.HiddenSize, activationName)
}

func (r *RNN) ResetState() {
	r.HiddenState = nil
}

func (r *RNN) getTimeStep(tensor *utils.Tensor, t int) *utils.Tensor {
	batchSize := tensor.Shape[0]
	featureSize := tensor.Shape[2]
	
	result := utils.NewTensor(batchSize, featureSize)
	for b := 0; b < batchSize; b++ {
		for f := 0; f < featureSize; f++ {
			srcIdx := b*tensor.Shape[1]*featureSize + t*featureSize + f
			dstIdx := b*featureSize + f
			result.Data[dstIdx] = tensor.Data[srcIdx]
		}
	}
	return result
}

func (r *RNN) setTimeStep(tensor *utils.Tensor, t int, value *utils.Tensor) {
	batchSize := tensor.Shape[0]
	featureSize := tensor.Shape[2]
	
	for b := 0; b < batchSize; b++ {
		for f := 0; f < featureSize; f++ {
			srcIdx := b*featureSize + f
			dstIdx := b*tensor.Shape[1]*featureSize + t*featureSize + f
			tensor.Data[dstIdx] = value.Data[srcIdx]
		}
	}
}

func (r *RNN) clipGradient(grad *utils.Tensor) {
	if r.GradientClipValue <= 0 {
		return
	}
	
	norm := 0.0
	for _, v := range grad.Data {
		norm += v * v
	}
	norm = math.Sqrt(norm)
	
	if norm > r.GradientClipValue {
		scale := r.GradientClipValue / norm
		for i := range grad.Data {
			grad.Data[i] *= scale
		}
	}
}