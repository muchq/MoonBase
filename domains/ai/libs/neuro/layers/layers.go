package layers

import (
	"fmt"
	
	"github.com/muchq/moonbase/domains/ai/libs/neuro/activations"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
	"gonum.org/v1/gonum/floats"
	"gonum.org/v1/gonum/stat"
)

type Layer interface {
	Forward(input *utils.Tensor, training bool) *utils.Tensor
	Backward(gradOutput *utils.Tensor) *utils.Tensor
	UpdateWeights(lr float64)
	GetParams() []*utils.Tensor
	GetGradients() []*utils.Tensor
	SetParams(params []*utils.Tensor)
	Name() string
}

type Dense struct {
	InputSize  int
	OutputSize int
	Weights    *utils.Tensor
	Bias       *utils.Tensor
	Activation activations.Activation
	
	Input      *utils.Tensor
	Output     *utils.Tensor
	GradW      *utils.Tensor
	GradB      *utils.Tensor
}

func NewDense(inputSize, outputSize int, activation activations.Activation) *Dense {
	return &Dense{
		InputSize:  inputSize,
		OutputSize: outputSize,
		Weights:    utils.XavierInit(inputSize, outputSize),
		Bias:       utils.NewTensor(outputSize),
		Activation: activation,
	}
}

func (d *Dense) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	d.Input = input.Copy()
	
	batchSize := 1
	if len(input.Shape) == 2 {
		batchSize = input.Shape[0]
	} else if len(input.Shape) == 1 {
		input = input.Reshape(1, input.Shape[0])
	}
	
	z := input.MatMul(d.Weights)
	
	for b := 0; b < batchSize; b++ {
		for j := 0; j < d.OutputSize; j++ {
			idx := b*d.OutputSize + j
			z.Data[idx] += d.Bias.Data[j]
		}
	}
	
	d.Output = z
	if d.Activation != nil {
		d.Output = d.Activation.Forward(z)
	}
	
	return d.Output
}

func (d *Dense) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	grad := gradOutput
	
	if d.Activation != nil {
		grad = d.Activation.Backward(gradOutput, d.Output)
	}
	
	batchSize := 1
	input := d.Input
	if len(d.Input.Shape) == 1 {
		input = d.Input.Reshape(1, d.Input.Shape[0])
		batchSize = 1
	} else {
		batchSize = d.Input.Shape[0]
	}
	
	d.GradW = input.Transpose().MatMul(grad)
	
	d.GradB = utils.NewTensor(d.OutputSize)
	for b := 0; b < batchSize; b++ {
		for j := 0; j < d.OutputSize; j++ {
			idx := b*d.OutputSize + j
			d.GradB.Data[j] += grad.Data[idx]
		}
	}
	
	gradInput := grad.MatMul(d.Weights.Transpose())
	
	if len(d.Input.Shape) == 1 {
		gradInput = gradInput.Reshape(d.InputSize)
	}
	
	return gradInput
}

func (d *Dense) UpdateWeights(lr float64) {
	if d.GradW == nil || d.GradB == nil {
		return
	}
	for i := range d.Weights.Data {
		d.Weights.Data[i] -= lr * d.GradW.Data[i]
	}
	for i := range d.Bias.Data {
		d.Bias.Data[i] -= lr * d.GradB.Data[i]
	}
}

func (d *Dense) GetParams() []*utils.Tensor {
	return []*utils.Tensor{d.Weights, d.Bias}
}

func (d *Dense) GetGradients() []*utils.Tensor {
	return []*utils.Tensor{d.GradW, d.GradB}
}

func (d *Dense) SetParams(params []*utils.Tensor) {
	if len(params) != 2 {
		panic("Dense layer expects 2 parameter tensors")
	}
	d.Weights = params[0]
	d.Bias = params[1]
}

func (d *Dense) Name() string {
	activationName := "None"
	if d.Activation != nil {
		activationName = d.Activation.Name()
	}
	return fmt.Sprintf("Dense(%d, %d, %s)", d.InputSize, d.OutputSize, activationName)
}

type Dropout struct {
	rate  float64
	mask  *utils.Tensor
	scale float64
}

func NewDropout(rate float64) *Dropout {
	return &Dropout{
		rate:  rate,
		scale: 1.0 / (1.0 - rate),
	}
}

func (d *Dropout) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	if !training || d.rate == 0 {
		return input.Copy()
	}
	
	d.mask = utils.NewTensor(input.Shape...)
	output := utils.NewTensor(input.Shape...)
	for i := range d.mask.Data {
		if utils.RandomFloat64() > d.rate {
			d.mask.Data[i] = 1.0
			output.Data[i] = input.Data[i] * d.scale
		} else {
			d.mask.Data[i] = 0.0
			output.Data[i] = 0.0
		}
	}
	
	return output
}

func (d *Dropout) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	if d.mask == nil {
		return gradOutput.Copy()
	}
	gradInput := utils.NewTensor(gradOutput.Shape...)
	for i := range gradInput.Data {
		if d.mask.Data[i] > 0 {
			gradInput.Data[i] = gradOutput.Data[i] * d.scale
		}
	}
	return gradInput
}

func (d *Dropout) UpdateWeights(lr float64) {}

func (d *Dropout) GetParams() []*utils.Tensor {
	return []*utils.Tensor{}
}

func (d *Dropout) GetGradients() []*utils.Tensor {
	return []*utils.Tensor{}
}

func (d *Dropout) SetParams(params []*utils.Tensor) {}

func (d *Dropout) Name() string {
	return fmt.Sprintf("Dropout(%.2f)", d.rate)
}

type BatchNorm struct {
	Size      int
	Epsilon   float64
	Momentum  float64
	Gamma     *utils.Tensor
	Beta      *utils.Tensor
	RunMean   *utils.Tensor
	RunVar    *utils.Tensor
	
	Input     *utils.Tensor
	Normed    *utils.Tensor
	Mean      *utils.Tensor
	Variance  *utils.Tensor
	GradGamma *utils.Tensor
	GradBeta  *utils.Tensor
}

func NewBatchNorm(size int) *BatchNorm {
	ones := utils.NewTensor(size)
	for i := range ones.Data {
		ones.Data[i] = 1.0
	}
	
	return &BatchNorm{
		Size:     size,
		Epsilon:  1e-5,
		Momentum: 0.9,
		Gamma:    ones,
		Beta:     utils.NewTensor(size),
		RunMean:  utils.NewTensor(size),
		RunVar:   ones.Copy(),
	}
}

func (b *BatchNorm) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	b.Input = input.Copy()
	
	if training {
		b.Mean = utils.NewTensor(b.Size)
		b.Variance = utils.NewTensor(b.Size)
		
		batchSize := input.Shape[0]
		
		// Compute mean and variance per feature using gonum
		for j := 0; j < b.Size; j++ {
			// Extract feature j across the batch
			feature := make([]float64, batchSize)
			for i := 0; i < batchSize; i++ {
				feature[i] = input.Data[i*b.Size + j]
			}
			
			// Use gonum for efficient statistical computation
			b.Mean.Data[j] = stat.Mean(feature, nil)
			b.Variance.Data[j] = stat.Variance(feature, nil)
		}
		
		// Update running statistics using vectorized operations
		// RunMean = momentum * RunMean + (1-momentum) * Mean
		temp := make([]float64, b.Size)
		copy(temp, b.Mean.Data)
		floats.Scale(1-b.Momentum, temp)
		floats.Scale(b.Momentum, b.RunMean.Data)
		floats.Add(b.RunMean.Data, temp)
		
		// RunVar = momentum * RunVar + (1-momentum) * Variance  
		copy(temp, b.Variance.Data)
		floats.Scale(1-b.Momentum, temp)
		floats.Scale(b.Momentum, b.RunVar.Data)
		floats.Add(b.RunVar.Data, temp)
	} else {
		b.Mean = b.RunMean
		b.Variance = b.RunVar
	}
	
	b.Normed = utils.NewTensor(input.Shape...)
	batchSize := 1
	if len(input.Shape) == 2 {
		batchSize = input.Shape[0]
	}
	
	for i := 0; i < batchSize; i++ {
		for j := 0; j < b.Size; j++ {
			idx := i*b.Size + j
			std := utils.Sqrt(b.Variance.Data[j] + b.Epsilon)
			b.Normed.Data[idx] = (input.Data[idx] - b.Mean.Data[j]) / std
		}
	}
	
	output := b.Normed.Copy()
	for i := 0; i < batchSize; i++ {
		for j := 0; j < b.Size; j++ {
			idx := i*b.Size + j
			output.Data[idx] = b.Gamma.Data[j]*b.Normed.Data[idx] + b.Beta.Data[j]
		}
	}
	
	return output
}

func (b *BatchNorm) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	batchSize := gradOutput.Shape[0]
	
	b.GradGamma = utils.NewTensor(b.Size)
	b.GradBeta = utils.NewTensor(b.Size)
	
	for i := 0; i < batchSize; i++ {
		for j := 0; j < b.Size; j++ {
			idx := i*b.Size + j
			b.GradGamma.Data[j] += gradOutput.Data[idx] * b.Normed.Data[idx]
			b.GradBeta.Data[j] += gradOutput.Data[idx]
		}
	}
	
	gradNormed := utils.NewTensor(gradOutput.Shape...)
	for i := 0; i < batchSize; i++ {
		for j := 0; j < b.Size; j++ {
			idx := i*b.Size + j
			gradNormed.Data[idx] = gradOutput.Data[idx] * b.Gamma.Data[j]
		}
	}
	
	gradVar := utils.NewTensor(b.Size)
	gradMean := utils.NewTensor(b.Size)
	
	for i := 0; i < batchSize; i++ {
		for j := 0; j < b.Size; j++ {
			idx := i*b.Size + j
			std := utils.Sqrt(b.Variance.Data[j] + b.Epsilon)
			diff := b.Input.Data[idx] - b.Mean.Data[j]
			gradVar.Data[j] += gradNormed.Data[idx] * diff * (-0.5) / (std * std * std)
			gradMean.Data[j] += gradNormed.Data[idx] * (-1.0) / std
		}
	}
	
	gradInput := utils.NewTensor(gradOutput.Shape...)
	for i := 0; i < batchSize; i++ {
		for j := 0; j < b.Size; j++ {
			idx := i*b.Size + j
			std := utils.Sqrt(b.Variance.Data[j] + b.Epsilon)
			diff := b.Input.Data[idx] - b.Mean.Data[j]
			gradInput.Data[idx] = gradNormed.Data[idx]/std + 
				gradVar.Data[j]*2*diff/float64(batchSize) + 
				gradMean.Data[j]/float64(batchSize)
		}
	}
	
	return gradInput
}

func (b *BatchNorm) UpdateWeights(lr float64) {
	for i := range b.Gamma.Data {
		b.Gamma.Data[i] -= lr * b.GradGamma.Data[i]
		b.Beta.Data[i] -= lr * b.GradBeta.Data[i]
	}
}

func (b *BatchNorm) GetParams() []*utils.Tensor {
	return []*utils.Tensor{b.Gamma, b.Beta, b.RunMean, b.RunVar}
}

func (b *BatchNorm) GetGradients() []*utils.Tensor {
	return []*utils.Tensor{b.GradGamma, b.GradBeta}
}

func (b *BatchNorm) SetParams(params []*utils.Tensor) {
	if len(params) != 4 {
		panic("BatchNorm layer expects 4 parameter tensors")
	}
	b.Gamma = params[0]
	b.Beta = params[1]
	b.RunMean = params[2]
	b.RunVar = params[3]
}

func (b *BatchNorm) Name() string {
	return fmt.Sprintf("BatchNorm(%d)", b.Size)
}