package layers

import (
	"fmt"
	"math"
	
	"github.com/muchq/moonbase/go/neuro/utils"
)

type GRU struct {
	InputSize  int
	HiddenSize int
	
	WeightIH *utils.Tensor
	WeightHH *utils.Tensor
	BiasIH   *utils.Tensor
	BiasHH   *utils.Tensor
	
	HiddenState *utils.Tensor
	
	InputCache   []*utils.Tensor
	HiddenCache  []*utils.Tensor
	ResetCache   []*utils.Tensor
	UpdateCache  []*utils.Tensor
	CandCache    []*utils.Tensor
	ResetHCache  []*utils.Tensor
	
	GradWeightIH *utils.Tensor
	GradWeightHH *utils.Tensor
	GradBiasIH   *utils.Tensor
	GradBiasHH   *utils.Tensor
	
	GradientClipValue float64
}

func NewGRU(inputSize, hiddenSize int) *GRU {
	gateSize := 3 * hiddenSize
	
	weightIH := utils.NewTensor(inputSize, gateSize)
	weightHH := utils.NewTensor(hiddenSize, gateSize)
	
	initGRUWeights(weightIH, inputSize, hiddenSize)
	initGRUWeights(weightHH, hiddenSize, hiddenSize)
	
	return &GRU{
		InputSize:         inputSize,
		HiddenSize:        hiddenSize,
		WeightIH:          weightIH,
		WeightHH:          weightHH,
		BiasIH:            utils.NewTensor(gateSize),
		BiasHH:            utils.NewTensor(gateSize),
		GradientClipValue: 5.0,
	}
}

func initGRUWeights(weight *utils.Tensor, fanIn, hiddenSize int) {
	std := 1.0 / math.Sqrt(float64(fanIn))
	for i := range weight.Data {
		weight.Data[i] = (utils.RandomFloat64()*2.0 - 1.0) * std
	}
}

func (g *GRU) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	var batchSize, seqLen int
	
	if len(input.Shape) == 3 {
		batchSize = input.Shape[0]
		seqLen = input.Shape[1]
		if input.Shape[2] != g.InputSize {
			panic(fmt.Sprintf("Expected input size %d, got %d", g.InputSize, input.Shape[2]))
		}
	} else if len(input.Shape) == 2 {
		batchSize = 1
		seqLen = input.Shape[0]
		if input.Shape[1] != g.InputSize {
			panic(fmt.Sprintf("Expected input size %d, got %d", g.InputSize, input.Shape[1]))
		}
		input = input.Reshape(1, seqLen, g.InputSize)
	} else {
		panic("GRU expects 2D (seq_len, input_size) or 3D (batch, seq_len, input_size) input")
	}
	
	if training {
		g.InputCache = make([]*utils.Tensor, seqLen)
		g.HiddenCache = make([]*utils.Tensor, seqLen+1)
		g.ResetCache = make([]*utils.Tensor, seqLen)
		g.UpdateCache = make([]*utils.Tensor, seqLen)
		g.CandCache = make([]*utils.Tensor, seqLen)
		g.ResetHCache = make([]*utils.Tensor, seqLen)
	}
	
	if g.HiddenState == nil || g.HiddenState.Shape[0] != batchSize {
		g.HiddenState = utils.NewTensor(batchSize, g.HiddenSize)
	}
	
	if training {
		g.HiddenCache[0] = g.HiddenState.Copy()
	}
	
	outputs := make([]*utils.Tensor, seqLen)
	
	for t := 0; t < seqLen; t++ {
		xt := g.getTimeStep(input, t)
		
		if training {
			g.InputCache[t] = xt.Copy()
		}
		
		gi := xt.MatMul(g.WeightIH)
		gh := g.HiddenState.MatMul(g.WeightHH)
		
		resetGate := g.extractGate(gi, gh, 0, batchSize)
		updateGate := g.extractGate(gi, gh, 1, batchSize)
		
		resetGate = sigmoidStable(resetGate)
		updateGate = sigmoidStable(updateGate)
		
		if training {
			g.ResetCache[t] = resetGate.Copy()
			g.UpdateCache[t] = updateGate.Copy()
		}
		
		resetHidden := resetGate.Mul(g.HiddenState)
		if training {
			g.ResetHCache[t] = resetHidden.Copy()
		}
		
		giCand := g.extractInputGate(gi, 2, batchSize)
		ghCand := resetHidden.MatMul(g.extractWeightHH(2))
		
		candidate := utils.NewTensor(batchSize, g.HiddenSize)
		for b := 0; b < batchSize; b++ {
			for h := 0; h < g.HiddenSize; h++ {
				idx := b*g.HiddenSize + h
				candidate.Data[idx] = giCand.Data[idx] + ghCand.Data[idx] + 
					g.BiasIH.Data[2*g.HiddenSize+h] + g.BiasHH.Data[2*g.HiddenSize+h]
			}
		}
		candidate = tanhActivation(candidate)
		
		if training {
			g.CandCache[t] = candidate.Copy()
		}
		
		oneMinusUpdate := utils.NewTensor(batchSize, g.HiddenSize)
		for i := range oneMinusUpdate.Data {
			oneMinusUpdate.Data[i] = 1.0 - updateGate.Data[i]
		}
		
		g.HiddenState = oneMinusUpdate.Mul(g.HiddenState).Add(updateGate.Mul(candidate))
		
		if training {
			g.HiddenCache[t+1] = g.HiddenState.Copy()
		}
		
		outputs[t] = g.HiddenState.Copy()
	}
	
	output := utils.NewTensor(batchSize, seqLen, g.HiddenSize)
	for t := 0; t < seqLen; t++ {
		for b := 0; b < batchSize; b++ {
			for h := 0; h < g.HiddenSize; h++ {
				srcIdx := b*g.HiddenSize + h
				dstIdx := b*seqLen*g.HiddenSize + t*g.HiddenSize + h
				output.Data[dstIdx] = outputs[t].Data[srcIdx]
			}
		}
	}
	
	return output
}

func (g *GRU) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	batchSize := gradOutput.Shape[0]
	seqLen := gradOutput.Shape[1]
	
	g.GradWeightIH = utils.NewTensor(g.InputSize, 3*g.HiddenSize)
	g.GradWeightHH = utils.NewTensor(g.HiddenSize, 3*g.HiddenSize)
	g.GradBiasIH = utils.NewTensor(3 * g.HiddenSize)
	g.GradBiasHH = utils.NewTensor(3 * g.HiddenSize)
	
	gradInput := utils.NewTensor(batchSize, seqLen, g.InputSize)
	gradHidden := utils.NewTensor(batchSize, g.HiddenSize)
	
	for t := seqLen - 1; t >= 0; t-- {
		gradT := g.getTimeStep(gradOutput, t)
		
		gradHiddenTotal := gradT.Add(gradHidden)
		
		oneMinusUpdate := utils.NewTensor(batchSize, g.HiddenSize)
		for i := range oneMinusUpdate.Data {
			oneMinusUpdate.Data[i] = 1.0 - g.UpdateCache[t].Data[i]
		}
		
		gradUpdate := gradHiddenTotal.Mul(g.CandCache[t]).Sub(
			gradHiddenTotal.Mul(g.HiddenCache[t]))
		gradCandidate := gradHiddenTotal.Mul(g.UpdateCache[t])
		gradHiddenPrev := gradHiddenTotal.Mul(oneMinusUpdate)
		
		gradCandidate = tanhBackwardActivation(gradCandidate, g.CandCache[t])
		
		gradResetH := gradCandidate.MatMul(g.extractWeightHH(2).Transpose())
		gradResetGate := gradResetH.Mul(g.HiddenCache[t])
		gradHiddenFromReset := gradResetH.Mul(g.ResetCache[t])
		
		gradUpdate = sigmoidBackwardStable(gradUpdate, g.UpdateCache[t])
		gradResetGate = sigmoidBackwardStable(gradResetGate, g.ResetCache[t])
		
		gradGates := g.stackGatesGRU(gradResetGate, gradUpdate, gradCandidate, batchSize)
		
		g.clipGradientGRU(gradGates)
		
		gradIH := g.InputCache[t].Transpose().MatMul(gradGates)
		for i := range g.GradWeightIH.Data {
			g.GradWeightIH.Data[i] += gradIH.Data[i]
		}
		
		gradGatesHH := g.extractGatesForBackward(gradGates, gradCandidate, batchSize)
		gradHH := g.HiddenCache[t].Transpose().MatMul(gradGatesHH)
		for i := range g.GradWeightHH.Data {
			g.GradWeightHH.Data[i] += gradHH.Data[i]
		}
		
		for b := 0; b < batchSize; b++ {
			for h := 0; h < g.HiddenSize; h++ {
				g.GradBiasIH.Data[0*g.HiddenSize+h] += gradResetGate.Data[b*g.HiddenSize+h]
				g.GradBiasIH.Data[1*g.HiddenSize+h] += gradUpdate.Data[b*g.HiddenSize+h]
				g.GradBiasIH.Data[2*g.HiddenSize+h] += gradCandidate.Data[b*g.HiddenSize+h]
				
				g.GradBiasHH.Data[0*g.HiddenSize+h] += gradResetGate.Data[b*g.HiddenSize+h]
				g.GradBiasHH.Data[1*g.HiddenSize+h] += gradUpdate.Data[b*g.HiddenSize+h]
				g.GradBiasHH.Data[2*g.HiddenSize+h] += gradCandidate.Data[b*g.HiddenSize+h]
			}
		}
		
		gradInputT := gradGates.MatMul(g.WeightIH.Transpose())
		g.setTimeStep(gradInput, t, gradInputT)
		
		gradHidden = gradHiddenPrev.Add(gradHiddenFromReset)
	}
	
	return gradInput
}

func (g *GRU) extractGate(gi, gh *utils.Tensor, gateIdx int, batchSize int) *utils.Tensor {
	gate := utils.NewTensor(batchSize, g.HiddenSize)
	offset := gateIdx * g.HiddenSize
	
	for b := 0; b < batchSize; b++ {
		for h := 0; h < g.HiddenSize; h++ {
			idxG := b*3*g.HiddenSize + offset + h
			idx := b*g.HiddenSize + h
			gate.Data[idx] = gi.Data[idxG] + gh.Data[idxG] + 
				g.BiasIH.Data[offset+h] + g.BiasHH.Data[offset+h]
		}
	}
	
	return gate
}

func (g *GRU) extractInputGate(gi *utils.Tensor, gateIdx int, batchSize int) *utils.Tensor {
	gate := utils.NewTensor(batchSize, g.HiddenSize)
	offset := gateIdx * g.HiddenSize
	
	for b := 0; b < batchSize; b++ {
		for h := 0; h < g.HiddenSize; h++ {
			idxG := b*3*g.HiddenSize + offset + h
			idx := b*g.HiddenSize + h
			gate.Data[idx] = gi.Data[idxG]
		}
	}
	
	return gate
}

func (g *GRU) extractWeightHH(gateIdx int) *utils.Tensor {
	weight := utils.NewTensor(g.HiddenSize, g.HiddenSize)
	offset := gateIdx * g.HiddenSize
	
	for i := 0; i < g.HiddenSize; i++ {
		for j := 0; j < g.HiddenSize; j++ {
			srcIdx := i*3*g.HiddenSize + offset + j
			dstIdx := i*g.HiddenSize + j
			weight.Data[dstIdx] = g.WeightHH.Data[srcIdx]
		}
	}
	
	return weight
}

func (g *GRU) extractGatesForBackward(gradGates, gradCandidate *utils.Tensor, batchSize int) *utils.Tensor {
	extracted := utils.NewTensor(batchSize, 3*g.HiddenSize)
	
	for b := 0; b < batchSize; b++ {
		for h := 0; h < g.HiddenSize; h++ {
			extracted.Data[b*3*g.HiddenSize+0*g.HiddenSize+h] = 
				gradGates.Data[b*3*g.HiddenSize+0*g.HiddenSize+h]
			extracted.Data[b*3*g.HiddenSize+1*g.HiddenSize+h] = 
				gradGates.Data[b*3*g.HiddenSize+1*g.HiddenSize+h]
			extracted.Data[b*3*g.HiddenSize+2*g.HiddenSize+h] = 
				gradCandidate.Data[b*g.HiddenSize+h]
		}
	}
	
	return extracted
}

func (g *GRU) stackGatesGRU(r, u, c *utils.Tensor, batchSize int) *utils.Tensor {
	gates := utils.NewTensor(batchSize, 3*g.HiddenSize)
	
	for b := 0; b < batchSize; b++ {
		for h := 0; h < g.HiddenSize; h++ {
			srcIdx := b*g.HiddenSize + h
			gates.Data[b*3*g.HiddenSize+0*g.HiddenSize+h] = r.Data[srcIdx]
			gates.Data[b*3*g.HiddenSize+1*g.HiddenSize+h] = u.Data[srcIdx]
			gates.Data[b*3*g.HiddenSize+2*g.HiddenSize+h] = c.Data[srcIdx]
		}
	}
	
	return gates
}


func (g *GRU) UpdateWeights(lr float64) {
	if g.GradWeightIH == nil {
		return
	}
	
	for i := range g.WeightIH.Data {
		g.WeightIH.Data[i] -= lr * g.GradWeightIH.Data[i]
	}
	for i := range g.WeightHH.Data {
		g.WeightHH.Data[i] -= lr * g.GradWeightHH.Data[i]
	}
	for i := range g.BiasIH.Data {
		g.BiasIH.Data[i] -= lr * g.GradBiasIH.Data[i]
	}
	for i := range g.BiasHH.Data {
		g.BiasHH.Data[i] -= lr * g.GradBiasHH.Data[i]
	}
}

func (g *GRU) GetParams() []*utils.Tensor {
	return []*utils.Tensor{g.WeightIH, g.WeightHH, g.BiasIH, g.BiasHH}
}

func (g *GRU) GetGradients() []*utils.Tensor {
	return []*utils.Tensor{g.GradWeightIH, g.GradWeightHH, g.GradBiasIH, g.GradBiasHH}
}

func (g *GRU) SetParams(params []*utils.Tensor) {
	if len(params) != 4 {
		panic("GRU layer expects 4 parameter tensors")
	}
	g.WeightIH = params[0]
	g.WeightHH = params[1]
	g.BiasIH = params[2]
	g.BiasHH = params[3]
}

func (g *GRU) Name() string {
	return fmt.Sprintf("GRU(%d, %d)", g.InputSize, g.HiddenSize)
}

func (g *GRU) ResetState() {
	g.HiddenState = nil
}

func (g *GRU) getTimeStep(tensor *utils.Tensor, t int) *utils.Tensor {
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

func (g *GRU) setTimeStep(tensor *utils.Tensor, t int, value *utils.Tensor) {
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

func (g *GRU) clipGradientGRU(grad *utils.Tensor) {
	if g.GradientClipValue <= 0 {
		return
	}
	
	norm := 0.0
	for _, v := range grad.Data {
		norm += v * v
	}
	norm = math.Sqrt(norm)
	
	if norm > g.GradientClipValue {
		scale := g.GradientClipValue / norm
		for i := range grad.Data {
			grad.Data[i] *= scale
		}
	}
}