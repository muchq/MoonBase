package layers

import (
	"math"
	"testing"
	
	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/utils"
)

func TestRNNForward(t *testing.T) {
	inputSize := 10
	hiddenSize := 20
	seqLen := 5
	batchSize := 2
	
	rnn := NewRNN(inputSize, hiddenSize, activations.NewTanh())
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%10) * 0.1
	}
	
	output := rnn.Forward(input, true)
	
	if len(output.Shape) != 3 {
		t.Errorf("Expected 3D output, got shape %v", output.Shape)
	}
	if output.Shape[0] != batchSize {
		t.Errorf("Expected batch size %d, got %d", batchSize, output.Shape[0])
	}
	if output.Shape[1] != seqLen {
		t.Errorf("Expected sequence length %d, got %d", seqLen, output.Shape[1])
	}
	if output.Shape[2] != hiddenSize {
		t.Errorf("Expected hidden size %d, got %d", hiddenSize, output.Shape[2])
	}
	
	for _, v := range output.Data {
		if math.IsNaN(v) || math.IsInf(v, 0) {
			t.Error("Output contains NaN or Inf values")
			break
		}
		if math.Abs(v) > 1.0 {
			t.Error("Tanh output should be in range [-1, 1]")
			break
		}
	}
}

func TestRNNBackward(t *testing.T) {
	inputSize := 5
	hiddenSize := 8
	seqLen := 3
	batchSize := 1
	
	rnn := NewRNN(inputSize, hiddenSize, activations.NewTanh())
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%5) * 0.2 - 0.5
	}
	
	output := rnn.Forward(input, true)
	
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.1
	}
	
	gradInput := rnn.Backward(gradOutput)
	
	if len(gradInput.Shape) != 3 {
		t.Errorf("Expected 3D gradient input, got shape %v", gradInput.Shape)
	}
	if gradInput.Shape[0] != batchSize {
		t.Errorf("Expected batch size %d, got %d", batchSize, gradInput.Shape[0])
	}
	if gradInput.Shape[1] != seqLen {
		t.Errorf("Expected sequence length %d, got %d", seqLen, gradInput.Shape[1])
	}
	if gradInput.Shape[2] != inputSize {
		t.Errorf("Expected input size %d, got %d", inputSize, gradInput.Shape[2])
	}
	
	if rnn.GradWeightIH == nil || rnn.GradWeightHH == nil {
		t.Error("Weight gradients not computed")
	}
	if rnn.GradBiasIH == nil || rnn.GradBiasHH == nil {
		t.Error("Bias gradients not computed")
	}
}

func TestRNNGradientCheck(t *testing.T) {
	inputSize := 3
	hiddenSize := 4
	seqLen := 2
	batchSize := 1
	epsilon := 1e-5
	tolerance := 1e-3
	
	rnn := NewRNN(inputSize, hiddenSize, activations.NewTanh())
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%3) * 0.3 - 0.4
	}
	
	output := rnn.Forward(input, true)
	loss := 0.0
	for _, v := range output.Data {
		loss += v * v
	}
	loss /= float64(len(output.Data))
	
	gradOutput := output.Copy()
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 2.0 * gradOutput.Data[i] / float64(len(gradOutput.Data))
	}
	
	_ = rnn.Backward(gradOutput)
	analyticalGrad := rnn.GradWeightIH.Data[0]
	
	originalWeight := rnn.WeightIH.Data[0]
	
	rnn.WeightIH.Data[0] = originalWeight + epsilon
	rnn.ResetState()
	outputPlus := rnn.Forward(input, false)
	lossPlus := 0.0
	for _, v := range outputPlus.Data {
		lossPlus += v * v
	}
	lossPlus /= float64(len(outputPlus.Data))
	
	rnn.WeightIH.Data[0] = originalWeight - epsilon
	rnn.ResetState()
	outputMinus := rnn.Forward(input, false)
	lossMinus := 0.0
	for _, v := range outputMinus.Data {
		lossMinus += v * v
	}
	lossMinus /= float64(len(outputMinus.Data))
	
	numericalGrad := (lossPlus - lossMinus) / (2 * epsilon)
	
	rnn.WeightIH.Data[0] = originalWeight
	
	relError := math.Abs(analyticalGrad-numericalGrad) / (math.Abs(analyticalGrad) + math.Abs(numericalGrad) + 1e-8)
	
	if relError > tolerance {
		t.Errorf("Gradient check failed: analytical=%f, numerical=%f, relative error=%f", 
			analyticalGrad, numericalGrad, relError)
	}
}

func TestRNNStateReset(t *testing.T) {
	rnn := NewRNN(5, 10, activations.NewTanh())
	
	input := utils.NewTensor(1, 3, 5)
	for i := range input.Data {
		input.Data[i] = float64(i) * 0.1
	}
	
	_ = rnn.Forward(input, false)
	
	if rnn.HiddenState == nil {
		t.Error("Hidden state should be set after forward pass")
	}
	
	rnn.ResetState()
	
	if rnn.HiddenState != nil {
		t.Error("Hidden state should be nil after reset")
	}
}

func TestRNNVariableSequenceLength(t *testing.T) {
	rnn := NewRNN(8, 16, activations.NewTanh())
	
	for seqLen := 1; seqLen <= 10; seqLen++ {
		input := utils.NewTensor(2, seqLen, 8)
		for i := range input.Data {
			input.Data[i] = float64(i%10) * 0.05
		}
		
		output := rnn.Forward(input, false)
		
		if output.Shape[1] != seqLen {
			t.Errorf("For seq_len=%d, expected output seq_len=%d, got %d", 
				seqLen, seqLen, output.Shape[1])
		}
		
		rnn.ResetState()
	}
}

func TestRNNGradientClipping(t *testing.T) {
	rnn := NewRNN(5, 10, activations.NewTanh())
	rnn.GradientClipValue = 1.0
	
	input := utils.NewTensor(1, 5, 5)
	for i := range input.Data {
		input.Data[i] = float64(i) * 10.0
	}
	
	output := rnn.Forward(input, true)
	
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 100.0
	}
	
	_ = rnn.Backward(gradOutput)
	
	maxGrad := 0.0
	for _, grad := range rnn.GetGradients() {
		if grad != nil {
			for _, v := range grad.Data {
				if math.Abs(v) > maxGrad {
					maxGrad = math.Abs(v)
				}
			}
		}
	}
	
	if math.IsInf(maxGrad, 0) || math.IsNaN(maxGrad) {
		t.Error("Gradients contain Inf or NaN after clipping")
	}
}

func BenchmarkRNNForward(b *testing.B) {
	rnn := NewRNN(128, 256, activations.NewTanh())
	input := utils.NewTensor(32, 20, 128)
	
	for i := range input.Data {
		input.Data[i] = float64(i%100) * 0.01
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = rnn.Forward(input, false)
		rnn.ResetState()
	}
}