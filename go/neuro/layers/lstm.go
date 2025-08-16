package layers

import (
	"fmt"
	"math"
	
	"github.com/muchq/moonbase/go/neuro/utils"
)

type LSTM struct {
	InputSize  int
	HiddenSize int
	
	WeightIH *utils.Tensor
	WeightHH *utils.Tensor
	BiasIH   *utils.Tensor
	BiasHH   *utils.Tensor
	
	HiddenState *utils.Tensor
	CellState   *utils.Tensor
	
	InputCache   []*utils.Tensor
	HiddenCache  []*utils.Tensor
	CellCache    []*utils.Tensor
	ForgetCache  []*utils.Tensor
	InputGCache  []*utils.Tensor
	CandCache    []*utils.Tensor
	OutputGCache []*utils.Tensor
	
	GradWeightIH *utils.Tensor
	GradWeightHH *utils.Tensor
	GradBiasIH   *utils.Tensor
	GradBiasHH   *utils.Tensor
	
	GradientClipValue float64
}

func NewLSTM(inputSize, hiddenSize int) *LSTM {
	gateSize := 4 * hiddenSize
	
	weightIH := utils.NewTensor(inputSize, gateSize)
	weightHH := utils.NewTensor(hiddenSize, gateSize)
	
	initLSTMWeights(weightIH, inputSize, hiddenSize)
	initLSTMWeights(weightHH, hiddenSize, hiddenSize)
	
	biasIH := utils.NewTensor(gateSize)
	biasHH := utils.NewTensor(gateSize)
	
	// Initialize forget gate bias to 1.0 (indices 0 to hiddenSize)
	for i := 0; i < hiddenSize; i++ {
		biasIH.Data[i] = 1.0
		biasHH.Data[i] = 1.0
	}
	
	return &LSTM{
		InputSize:         inputSize,
		HiddenSize:        hiddenSize,
		WeightIH:          weightIH,
		WeightHH:          weightHH,
		BiasIH:            biasIH,
		BiasHH:            biasHH,
		GradientClipValue: 5.0,
	}
}

func initLSTMWeights(weight *utils.Tensor, fanIn, hiddenSize int) {
	std := 1.0 / math.Sqrt(float64(fanIn))
	for i := range weight.Data {
		weight.Data[i] = (utils.RandomFloat64()*2.0 - 1.0) * std
	}
}

func (l *LSTM) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	var batchSize, seqLen int
	
	if len(input.Shape) == 3 {
		batchSize = input.Shape[0]
		seqLen = input.Shape[1]
		if input.Shape[2] != l.InputSize {
			panic(fmt.Sprintf("Expected input size %d, got %d", l.InputSize, input.Shape[2]))
		}
	} else if len(input.Shape) == 2 {
		batchSize = 1
		seqLen = input.Shape[0]
		if input.Shape[1] != l.InputSize {
			panic(fmt.Sprintf("Expected input size %d, got %d", l.InputSize, input.Shape[1]))
		}
		input = input.Reshape(1, seqLen, l.InputSize)
	} else {
		panic("LSTM expects 2D (seq_len, input_size) or 3D (batch, seq_len, input_size) input")
	}
	
	if training {
		l.InputCache = make([]*utils.Tensor, seqLen)
		l.HiddenCache = make([]*utils.Tensor, seqLen+1)
		l.CellCache = make([]*utils.Tensor, seqLen+1)
		l.ForgetCache = make([]*utils.Tensor, seqLen)
		l.InputGCache = make([]*utils.Tensor, seqLen)
		l.CandCache = make([]*utils.Tensor, seqLen)
		l.OutputGCache = make([]*utils.Tensor, seqLen)
	}
	
	if l.HiddenState == nil || l.HiddenState.Shape[0] != batchSize {
		l.HiddenState = utils.NewTensor(batchSize, l.HiddenSize)
		l.CellState = utils.NewTensor(batchSize, l.HiddenSize)
	}
	
	if training {
		l.HiddenCache[0] = l.HiddenState.Copy()
		l.CellCache[0] = l.CellState.Copy()
	}
	
	outputs := make([]*utils.Tensor, seqLen)
	
	for t := 0; t < seqLen; t++ {
		xt := l.getTimeStep(input, t)
		
		if training {
			l.InputCache[t] = xt.Copy()
		}
		
		gates := l.computeGates(xt, l.HiddenState)
		
		forgetGate := l.extractGate(gates, 0, batchSize)
		inputGate := l.extractGate(gates, 1, batchSize)
		candidate := l.extractGate(gates, 2, batchSize)
		outputGate := l.extractGate(gates, 3, batchSize)
		
		forgetGate = sigmoidStable(forgetGate)
		inputGate = sigmoidStable(inputGate)
		candidate = tanhActivation(candidate)
		outputGate = sigmoidStable(outputGate)
		
		if training {
			l.ForgetCache[t] = forgetGate.Copy()
			l.InputGCache[t] = inputGate.Copy()
			l.CandCache[t] = candidate.Copy()
			l.OutputGCache[t] = outputGate.Copy()
		}
		
		l.CellState = l.CellState.Mul(forgetGate).Add(inputGate.Mul(candidate))
		
		cellTanh := tanhActivation(l.CellState)
		l.HiddenState = outputGate.Mul(cellTanh)
		
		if training {
			l.HiddenCache[t+1] = l.HiddenState.Copy()
			l.CellCache[t+1] = l.CellState.Copy()
		}
		
		outputs[t] = l.HiddenState.Copy()
	}
	
	output := utils.NewTensor(batchSize, seqLen, l.HiddenSize)
	for t := 0; t < seqLen; t++ {
		for b := 0; b < batchSize; b++ {
			for h := 0; h < l.HiddenSize; h++ {
				srcIdx := b*l.HiddenSize + h
				dstIdx := b*seqLen*l.HiddenSize + t*l.HiddenSize + h
				output.Data[dstIdx] = outputs[t].Data[srcIdx]
			}
		}
	}
	
	return output
}

func (l *LSTM) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	batchSize := gradOutput.Shape[0]
	seqLen := gradOutput.Shape[1]
	
	l.GradWeightIH = utils.NewTensor(l.InputSize, 4*l.HiddenSize)
	l.GradWeightHH = utils.NewTensor(l.HiddenSize, 4*l.HiddenSize)
	l.GradBiasIH = utils.NewTensor(4 * l.HiddenSize)
	l.GradBiasHH = utils.NewTensor(4 * l.HiddenSize)
	
	gradInput := utils.NewTensor(batchSize, seqLen, l.InputSize)
	gradHidden := utils.NewTensor(batchSize, l.HiddenSize)
	gradCell := utils.NewTensor(batchSize, l.HiddenSize)
	
	for t := seqLen - 1; t >= 0; t-- {
		gradT := l.getTimeStep(gradOutput, t)
		
		gradHiddenTotal := gradT.Add(gradHidden)
		
		cellTanh := tanhActivation(l.CellCache[t+1])
		gradOutputGate := gradHiddenTotal.Mul(cellTanh)
		gradCellFromHidden := gradHiddenTotal.Mul(l.OutputGCache[t])
		gradCellFromHidden = tanhBackwardActivation(gradCellFromHidden, cellTanh)
		
		gradCellTotal := gradCell.Add(gradCellFromHidden)
		
		gradForget := gradCellTotal.Mul(l.CellCache[t])
		gradInputG := gradCellTotal.Mul(l.CandCache[t])
		gradCandidate := gradCellTotal.Mul(l.InputGCache[t])
		gradCellPrev := gradCellTotal.Mul(l.ForgetCache[t])
		
		gradForget = sigmoidBackwardStable(gradForget, l.ForgetCache[t])
		gradInputG = sigmoidBackwardStable(gradInputG, l.InputGCache[t])
		gradCandidate = tanhBackwardActivation(gradCandidate, l.CandCache[t])
		gradOutputGate = sigmoidBackwardStable(gradOutputGate, l.OutputGCache[t])
		
		gradGates := l.stackGates(gradForget, gradInputG, gradCandidate, gradOutputGate, batchSize)
		
		l.clipGradient(gradGates)
		
		gradIH := l.InputCache[t].Transpose().MatMul(gradGates)
		for i := range l.GradWeightIH.Data {
			l.GradWeightIH.Data[i] += gradIH.Data[i]
		}
		
		gradHH := l.HiddenCache[t].Transpose().MatMul(gradGates)
		for i := range l.GradWeightHH.Data {
			l.GradWeightHH.Data[i] += gradHH.Data[i]
		}
		
		for b := 0; b < batchSize; b++ {
			for g := 0; g < 4*l.HiddenSize; g++ {
				idx := b*4*l.HiddenSize + g
				l.GradBiasIH.Data[g] += gradGates.Data[idx]
				l.GradBiasHH.Data[g] += gradGates.Data[idx]
			}
		}
		
		gradInputT := gradGates.MatMul(l.WeightIH.Transpose())
		l.setTimeStep(gradInput, t, gradInputT)
		
		gradHidden = gradGates.MatMul(l.WeightHH.Transpose())
		gradCell = gradCellPrev
	}
	
	return gradInput
}

func (l *LSTM) computeGates(x, h *utils.Tensor) *utils.Tensor {
	ih := x.MatMul(l.WeightIH)
	hh := h.MatMul(l.WeightHH)
	
	batchSize := x.Shape[0]
	gateSize := 4 * l.HiddenSize
	
	gates := utils.NewTensor(batchSize, gateSize)
	
	for b := 0; b < batchSize; b++ {
		for g := 0; g < gateSize; g++ {
			idx := b*gateSize + g
			gates.Data[idx] = ih.Data[idx] + hh.Data[idx] + l.BiasIH.Data[g] + l.BiasHH.Data[g]
		}
	}
	
	return gates
}

func (l *LSTM) extractGate(gates *utils.Tensor, gateIdx int, batchSize int) *utils.Tensor {
	gate := utils.NewTensor(batchSize, l.HiddenSize)
	offset := gateIdx * l.HiddenSize
	
	for b := 0; b < batchSize; b++ {
		for h := 0; h < l.HiddenSize; h++ {
			srcIdx := b*4*l.HiddenSize + offset + h
			dstIdx := b*l.HiddenSize + h
			gate.Data[dstIdx] = gates.Data[srcIdx]
		}
	}
	
	return gate
}

func (l *LSTM) stackGates(f, i, c, o *utils.Tensor, batchSize int) *utils.Tensor {
	gates := utils.NewTensor(batchSize, 4*l.HiddenSize)
	
	for b := 0; b < batchSize; b++ {
		for h := 0; h < l.HiddenSize; h++ {
			srcIdx := b*l.HiddenSize + h
			gates.Data[b*4*l.HiddenSize+0*l.HiddenSize+h] = f.Data[srcIdx]
			gates.Data[b*4*l.HiddenSize+1*l.HiddenSize+h] = i.Data[srcIdx]
			gates.Data[b*4*l.HiddenSize+2*l.HiddenSize+h] = c.Data[srcIdx]
			gates.Data[b*4*l.HiddenSize+3*l.HiddenSize+h] = o.Data[srcIdx]
		}
	}
	
	return gates
}


func (l *LSTM) UpdateWeights(lr float64) {
	if l.GradWeightIH == nil {
		return
	}
	
	for i := range l.WeightIH.Data {
		l.WeightIH.Data[i] -= lr * l.GradWeightIH.Data[i]
	}
	for i := range l.WeightHH.Data {
		l.WeightHH.Data[i] -= lr * l.GradWeightHH.Data[i]
	}
	for i := range l.BiasIH.Data {
		l.BiasIH.Data[i] -= lr * l.GradBiasIH.Data[i]
	}
	for i := range l.BiasHH.Data {
		l.BiasHH.Data[i] -= lr * l.GradBiasHH.Data[i]
	}
}

func (l *LSTM) GetParams() []*utils.Tensor {
	return []*utils.Tensor{l.WeightIH, l.WeightHH, l.BiasIH, l.BiasHH}
}

func (l *LSTM) GetGradients() []*utils.Tensor {
	return []*utils.Tensor{l.GradWeightIH, l.GradWeightHH, l.GradBiasIH, l.GradBiasHH}
}

func (l *LSTM) SetParams(params []*utils.Tensor) {
	if len(params) != 4 {
		panic("LSTM layer expects 4 parameter tensors")
	}
	l.WeightIH = params[0]
	l.WeightHH = params[1]
	l.BiasIH = params[2]
	l.BiasHH = params[3]
}

func (l *LSTM) Name() string {
	return fmt.Sprintf("LSTM(%d, %d)", l.InputSize, l.HiddenSize)
}

func (l *LSTM) ResetState() {
	l.HiddenState = nil
	l.CellState = nil
}

func (l *LSTM) getTimeStep(tensor *utils.Tensor, t int) *utils.Tensor {
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

func (l *LSTM) setTimeStep(tensor *utils.Tensor, t int, value *utils.Tensor) {
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

func (l *LSTM) clipGradient(grad *utils.Tensor) {
	if l.GradientClipValue <= 0 {
		return
	}
	
	norm := 0.0
	for _, v := range grad.Data {
		norm += v * v
	}
	norm = math.Sqrt(norm)
	
	if norm > l.GradientClipValue {
		scale := l.GradientClipValue / norm
		for i := range grad.Data {
			grad.Data[i] *= scale
		}
	}
}