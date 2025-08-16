package embeddings

import (
	"github.com/muchq/moonbase/go/neuro/utils"
	"math"
)

type MultiHeadAttention struct {
	numHeads       int
	dModel         int
	dK             int
	dV             int
	qLinear        *LinearProjection
	kLinear        *LinearProjection
	vLinear        *LinearProjection
	outLinear      *LinearProjection
	dropout        float64
	scale          float64
	attentionCache map[string]*utils.Tensor
}

func NewMultiHeadAttention(numHeads, dModel int, dropout float64) *MultiHeadAttention {
	if dModel%numHeads != 0 {
		panic("d_model must be divisible by num_heads")
	}

	dK := dModel / numHeads
	dV := dModel / numHeads

	return &MultiHeadAttention{
		numHeads:       numHeads,
		dModel:         dModel,
		dK:             dK,
		dV:             dV,
		qLinear:        NewLinearProjection(dModel, dModel),
		kLinear:        NewLinearProjection(dModel, dModel),
		vLinear:        NewLinearProjection(dModel, dModel),
		outLinear:      NewLinearProjection(dModel, dModel),
		dropout:        dropout,
		scale:          1.0 / math.Sqrt(float64(dK)),
		attentionCache: make(map[string]*utils.Tensor),
	}
}

func (mha *MultiHeadAttention) Forward(query, key, value *utils.Tensor, mask *utils.Tensor, training bool) *utils.Tensor {
	batchSize := query.Shape[0]
	seqLen := query.Shape[1]

	Q := mha.qLinear.Forward(query)
	K := mha.kLinear.Forward(key)
	V := mha.vLinear.Forward(value)

	Q = mha.reshapeForHeads(Q, batchSize, seqLen)
	K = mha.reshapeForHeads(K, batchSize, key.Shape[1])
	V = mha.reshapeForHeads(V, batchSize, value.Shape[1])

	scores := mha.scaledDotProductAttention(Q, K, V, mask)

	concat := mha.concatenateHeads(scores, batchSize, seqLen)

	output := mha.outLinear.Forward(concat)

	if training && mha.dropout > 0 {
		output = applyDropout(output, mha.dropout)
	}

	return output
}

func (mha *MultiHeadAttention) reshapeForHeads(x *utils.Tensor, batchSize, seqLen int) *utils.Tensor {
	newShape := []int{batchSize, seqLen, mha.numHeads, mha.dK}
	reshaped := utils.Reshape(x, newShape)

	transposed := utils.Transpose(reshaped, []int{0, 2, 1, 3})
	return transposed
}

func (mha *MultiHeadAttention) scaledDotProductAttention(Q, K, V, mask *utils.Tensor) *utils.Tensor {
	scores := utils.MatMul(Q, utils.Transpose(K, []int{0, 1, 3, 2}))

	scaledScores := utils.Scale(scores, mha.scale)

	if mask != nil {
		// Reshape mask to broadcast with attention scores
		// mask is [batch, seq_len], scores are [batch, heads, seq_len, seq_len]
		// We need to expand mask to [batch, 1, 1, seq_len] and broadcast
		if len(mask.Shape) == 2 && len(scaledScores.Shape) == 4 {
			// Reshape mask from [batch, seq_len] to [batch, 1, 1, seq_len]
			batchSize := mask.Shape[0]
			seqLen := mask.Shape[1]
			expandedMask := utils.NewTensor(batchSize, 1, 1, seqLen)
			for b := 0; b < batchSize; b++ {
				for s := 0; s < seqLen; s++ {
					expandedMask.Set(mask.Get(b, s), b, 0, 0, s)
				}
			}

			// Create mask values tensor with same shape as scores
			maskValues := utils.NewTensor(scaledScores.Shape...)
			for b := 0; b < scaledScores.Shape[0]; b++ {
				for h := 0; h < scaledScores.Shape[1]; h++ {
					for i := 0; i < scaledScores.Shape[2]; i++ {
						for j := 0; j < scaledScores.Shape[3]; j++ {
							// If mask position is 0, set to -1e9, otherwise 0
							maskVal := expandedMask.Get(b, 0, 0, j)
							if maskVal == 0 {
								maskValues.Set(-1e9, b, h, i, j)
							}
						}
					}
				}
			}

			scaledScores = utils.Add(scaledScores, maskValues)
		} else {
			// Fallback for other mask shapes
			maskValue := utils.NewTensorFromData([]float64{-1e9}, 1)
			scaledScores = utils.Add(scaledScores, utils.Multiply(mask, maskValue))
		}
	}

	attentionWeights := softmaxAlongLastDim(scaledScores)

	mha.attentionCache["weights"] = attentionWeights

	output := utils.MatMul(attentionWeights, V)

	return output
}

func (mha *MultiHeadAttention) concatenateHeads(x *utils.Tensor, batchSize, seqLen int) *utils.Tensor {
	transposed := utils.Transpose(x, []int{0, 2, 1, 3})

	newShape := []int{batchSize, seqLen, mha.dModel}
	concat := utils.Reshape(transposed, newShape)

	return concat
}

type LinearProjection struct {
	weight    *utils.Tensor
	bias      *utils.Tensor
	inputDim  int
	outputDim int
}

func NewLinearProjection(inputDim, outputDim int) *LinearProjection {
	weight := utils.XavierUniform([]int{inputDim, outputDim})
	bias := utils.Zeros([]int{outputDim})

	return &LinearProjection{
		weight:    weight,
		bias:      bias,
		inputDim:  inputDim,
		outputDim: outputDim,
	}
}

func (lp *LinearProjection) Forward(x *utils.Tensor) *utils.Tensor {
	output := utils.MatMul(x, lp.weight)
	output = utils.AddBias(output, lp.bias)
	return output
}

type TransformerBlock struct {
	attention   *MultiHeadAttention
	feedForward *FeedForward
	layerNorm1  *LayerNorm
	layerNorm2  *LayerNorm
	dropout     float64
}

func NewTransformerBlock(dModel, numHeads, dFF int, dropout float64) *TransformerBlock {
	return &TransformerBlock{
		attention:   NewMultiHeadAttention(numHeads, dModel, dropout),
		feedForward: NewFeedForward(dModel, dFF, dropout),
		layerNorm1:  NewLayerNorm(dModel),
		layerNorm2:  NewLayerNorm(dModel),
		dropout:     dropout,
	}
}

func (tb *TransformerBlock) Forward(x *utils.Tensor, mask *utils.Tensor, training bool) *utils.Tensor {
	attnOutput := tb.attention.Forward(x, x, x, mask, training)

	if training && tb.dropout > 0 {
		attnOutput = applyDropout(attnOutput, tb.dropout)
	}

	x = utils.Add(x, attnOutput)
	x = tb.layerNorm1.Forward(x)

	ffOutput := tb.feedForward.Forward(x, training)

	if training && tb.dropout > 0 {
		ffOutput = applyDropout(ffOutput, tb.dropout)
	}

	x = utils.Add(x, ffOutput)
	x = tb.layerNorm2.Forward(x)

	return x
}

type FeedForward struct {
	linear1 *LinearProjection
	linear2 *LinearProjection
	dropout float64
}

func NewFeedForward(dModel, dFF int, dropout float64) *FeedForward {
	return &FeedForward{
		linear1: NewLinearProjection(dModel, dFF),
		linear2: NewLinearProjection(dFF, dModel),
		dropout: dropout,
	}
}

func (ff *FeedForward) Forward(x *utils.Tensor, training bool) *utils.Tensor {
	x = ff.linear1.Forward(x)
	x = gelu(x)

	if training && ff.dropout > 0 {
		x = applyDropout(x, ff.dropout)
	}

	x = ff.linear2.Forward(x)
	return x
}

type LayerNorm struct {
	gamma            *utils.Tensor
	beta             *utils.Tensor
	epsilon          float64
	normalized_shape int
}

func NewLayerNorm(normalizedShape int) *LayerNorm {
	gamma := utils.Ones([]int{normalizedShape})
	beta := utils.Zeros([]int{normalizedShape})

	return &LayerNorm{
		gamma:            gamma,
		beta:             beta,
		epsilon:          1e-5,
		normalized_shape: normalizedShape,
	}
}

func (ln *LayerNorm) Forward(x *utils.Tensor) *utils.Tensor {
	mean := computeMean(x, -1, true)
	variance := computeVariance(x, mean, -1, true)

	std := utils.SqrtTensor(utils.AddScalar(variance, ln.epsilon))

	normalized := utils.Divide(utils.Subtract(x, mean), std)

	output := utils.Add(utils.Multiply(normalized, ln.gamma), ln.beta)

	return output
}

type PositionalEncoding struct {
	dModel   int
	maxLen   int
	encoding *utils.Tensor
}

func NewPositionalEncoding(dModel, maxLen int) *PositionalEncoding {
	pe := &PositionalEncoding{
		dModel: dModel,
		maxLen: maxLen,
	}

	pe.encoding = pe.createEncoding()

	return pe
}

func (pe *PositionalEncoding) createEncoding() *utils.Tensor {
	encoding := utils.Zeros([]int{pe.maxLen, pe.dModel})

	for pos := 0; pos < pe.maxLen; pos++ {
		for i := 0; i < pe.dModel; i += 2 {
			angle := float64(pos) / math.Pow(10000, float64(i)/float64(pe.dModel))
			encoding.Data[pos*pe.dModel+i] = math.Sin(angle)
			if i+1 < pe.dModel {
				encoding.Data[pos*pe.dModel+i+1] = math.Cos(angle)
			}
		}
	}

	return encoding
}

func (pe *PositionalEncoding) Forward(x *utils.Tensor) *utils.Tensor {
	seqLen := x.Shape[1]

	posEncoding := utils.SliceAlongDim(pe.encoding, 0, seqLen, 0)

	posEncoding = utils.Unsqueeze(posEncoding, 0)

	return utils.Add(x, posEncoding)
}

func gelu(x *utils.Tensor) *utils.Tensor {
	output := utils.NewTensor(x.Shape...)

	for i := range x.Data {
		val := x.Data[i]
		output.Data[i] = 0.5 * val * (1 + math.Tanh(math.Sqrt(2/math.Pi)*(val+0.044715*val*val*val)))
	}

	return output
}

func applyDropout(x *utils.Tensor, rate float64) *utils.Tensor {
	if rate <= 0 {
		return x
	}

	mask := utils.RandomBernoulli(x.Shape, 1-rate)
	scale := 1.0 / (1.0 - rate)

	return utils.Multiply(x, utils.Scale(mask, scale))
}

func softmaxAlongLastDim(x *utils.Tensor) *utils.Tensor {
	lastDim := len(x.Shape) - 1
	output := utils.NewTensor(x.Shape...)

	strides := make([]int, len(x.Shape))
	strides[len(strides)-1] = 1
	for i := len(strides) - 2; i >= 0; i-- {
		strides[i] = strides[i+1] * x.Shape[i+1]
	}

	numElements := 1
	for i := 0; i < lastDim; i++ {
		numElements *= x.Shape[i]
	}

	dimSize := x.Shape[lastDim]

	for i := 0; i < numElements; i++ {
		baseIdx := i * dimSize

		maxVal := x.Data[baseIdx]
		for j := 1; j < dimSize; j++ {
			if x.Data[baseIdx+j] > maxVal {
				maxVal = x.Data[baseIdx+j]
			}
		}

		sum := 0.0
		for j := 0; j < dimSize; j++ {
			output.Data[baseIdx+j] = math.Exp(x.Data[baseIdx+j] - maxVal)
			sum += output.Data[baseIdx+j]
		}

		for j := 0; j < dimSize; j++ {
			output.Data[baseIdx+j] /= sum
		}
	}

	return output
}

func computeMean(x *utils.Tensor, axis int, keepDims bool) *utils.Tensor {
	if axis < 0 {
		axis = len(x.Shape) + axis
	}

	// For now, only support mean along last dimension for LayerNorm
	if axis != len(x.Shape)-1 {
		panic("computeMean currently only supports reduction along last dimension")
	}

	// Compute output shape
	outputShape := make([]int, 0, len(x.Shape))
	for i, dim := range x.Shape {
		if i != axis {
			outputShape = append(outputShape, dim)
		} else if keepDims {
			outputShape = append(outputShape, 1)
		}
	}

	output := utils.Zeros(outputShape)

	// Calculate mean along the last dimension
	if len(x.Shape) == 2 {
		// 2D case
		for i := 0; i < x.Shape[0]; i++ {
			sum := 0.0
			for j := 0; j < x.Shape[1]; j++ {
				sum += x.Get(i, j)
			}
			mean := sum / float64(x.Shape[1])
			if keepDims {
				output.Set(mean, i, 0)
			} else {
				output.Data[i] = mean
			}
		}
	} else if len(x.Shape) == 3 {
		// 3D case
		for i := 0; i < x.Shape[0]; i++ {
			for j := 0; j < x.Shape[1]; j++ {
				sum := 0.0
				for k := 0; k < x.Shape[2]; k++ {
					sum += x.Get(i, j, k)
				}
				mean := sum / float64(x.Shape[2])
				if keepDims {
					output.Set(mean, i, j, 0)
				} else {
					output.Set(mean, i, j)
				}
			}
		}
	} else {
		panic("computeMean only supports 2D and 3D tensors")
	}

	return output
}

func computeVariance(x, mean *utils.Tensor, axis int, keepDims bool) *utils.Tensor {
	diff := utils.Subtract(x, mean)
	squared := utils.Multiply(diff, diff)
	return computeMean(squared, axis, keepDims)
}
