package layers

import (
	"math"
	"testing"
	
	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
)

func TestGRUForward(t *testing.T) {
	inputSize := 10
	hiddenSize := 20
	seqLen := 5
	batchSize := 2
	
	gru := NewGRU(inputSize, hiddenSize)
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%10) * 0.1
	}
	
	output := gru.Forward(input, true)
	
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
			t.Error("GRU output should be in range [-1, 1] due to tanh activation")
			break
		}
	}
}

func TestGRUBackward(t *testing.T) {
	inputSize := 5
	hiddenSize := 8
	seqLen := 3
	batchSize := 1
	
	gru := NewGRU(inputSize, hiddenSize)
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%5) * 0.2 - 0.5
	}
	
	output := gru.Forward(input, true)
	
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.1
	}
	
	gradInput := gru.Backward(gradOutput)
	
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
	
	if gru.GradWeightIH == nil || gru.GradWeightHH == nil {
		t.Error("Weight gradients not computed")
	}
	if gru.GradBiasIH == nil || gru.GradBiasHH == nil {
		t.Error("Bias gradients not computed")
	}
	
	if gru.GradWeightIH.Shape[0] != inputSize || gru.GradWeightIH.Shape[1] != 3*hiddenSize {
		t.Errorf("GradWeightIH has wrong shape: %v", gru.GradWeightIH.Shape)
	}
	if gru.GradWeightHH.Shape[0] != hiddenSize || gru.GradWeightHH.Shape[1] != 3*hiddenSize {
		t.Errorf("GradWeightHH has wrong shape: %v", gru.GradWeightHH.Shape)
	}
}

func TestGRUGradientCheck(t *testing.T) {
	inputSize := 3
	hiddenSize := 4
	seqLen := 2
	batchSize := 1
	epsilon := 1e-5
	tolerance := 1e-3
	
	gru := NewGRU(inputSize, hiddenSize)
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%3) * 0.3 - 0.4
	}
	
	output := gru.Forward(input, true)
	loss := 0.0
	for _, v := range output.Data {
		loss += v * v
	}
	loss /= float64(len(output.Data))
	
	gradOutput := output.Copy()
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 2.0 * gradOutput.Data[i] / float64(len(gradOutput.Data))
	}
	
	_ = gru.Backward(gradOutput)
	analyticalGrad := gru.GradWeightIH.Data[0]
	
	originalWeight := gru.WeightIH.Data[0]
	
	gru.WeightIH.Data[0] = originalWeight + epsilon
	gru.ResetState()
	outputPlus := gru.Forward(input, false)
	lossPlus := 0.0
	for _, v := range outputPlus.Data {
		lossPlus += v * v
	}
	lossPlus /= float64(len(outputPlus.Data))
	
	gru.WeightIH.Data[0] = originalWeight - epsilon
	gru.ResetState()
	outputMinus := gru.Forward(input, false)
	lossMinus := 0.0
	for _, v := range outputMinus.Data {
		lossMinus += v * v
	}
	lossMinus /= float64(len(outputMinus.Data))
	
	numericalGrad := (lossPlus - lossMinus) / (2 * epsilon)
	
	gru.WeightIH.Data[0] = originalWeight
	
	relError := math.Abs(analyticalGrad-numericalGrad) / (math.Abs(analyticalGrad) + math.Abs(numericalGrad) + 1e-8)
	
	if relError > tolerance {
		t.Errorf("Gradient check failed: analytical=%f, numerical=%f, relative error=%f", 
			analyticalGrad, numericalGrad, relError)
	}
}

func TestGRUStateReset(t *testing.T) {
	gru := NewGRU(5, 10)
	
	input := utils.NewTensor(1, 3, 5)
	for i := range input.Data {
		input.Data[i] = float64(i) * 0.1
	}
	
	_ = gru.Forward(input, false)
	
	if gru.HiddenState == nil {
		t.Error("Hidden state should be set after forward pass")
	}
	
	gru.ResetState()
	
	if gru.HiddenState != nil {
		t.Error("Hidden state should be nil after reset")
	}
}

func TestGRUGateBehavior(t *testing.T) {
	gru := NewGRU(5, 10)
	
	inputConstant := utils.NewTensor(1, 5, 5)
	for i := range inputConstant.Data {
		inputConstant.Data[i] = 0.5
	}
	
	inputZero := utils.NewTensor(1, 5, 5)
	
	output1 := gru.Forward(inputConstant, false)
	prevOutput := output1.Copy()
	
	output2 := gru.Forward(inputZero, false)
	
	changed := false
	for i := range output2.Data {
		if math.Abs(output2.Data[i]-prevOutput.Data[i]) > 1e-6 {
			changed = true
			break
		}
	}
	
	if !changed {
		t.Error("GRU output should change with different inputs due to update and reset gates")
	}
}

func TestGRUVariableSequenceLength(t *testing.T) {
	gru := NewGRU(8, 16)
	
	for seqLen := 1; seqLen <= 10; seqLen++ {
		input := utils.NewTensor(2, seqLen, 8)
		for i := range input.Data {
			input.Data[i] = float64(i%10) * 0.05
		}
		
		output := gru.Forward(input, false)
		
		if output.Shape[1] != seqLen {
			t.Errorf("For seq_len=%d, expected output seq_len=%d, got %d", 
				seqLen, seqLen, output.Shape[1])
		}
		
		gru.ResetState()
	}
}

func TestGRUGradientClipping(t *testing.T) {
	gru := NewGRU(5, 10)
	gru.GradientClipValue = 1.0
	
	input := utils.NewTensor(1, 5, 5)
	for i := range input.Data {
		input.Data[i] = float64(i) * 10.0
	}
	
	output := gru.Forward(input, true)
	
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 100.0
	}
	
	_ = gru.Backward(gradOutput)
	
	maxGrad := 0.0
	for _, grad := range gru.GetGradients() {
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

func TestGRUVsLSTMParameters(t *testing.T) {
	inputSize := 10
	hiddenSize := 20
	
	gru := NewGRU(inputSize, hiddenSize)
	lstm := NewLSTM(inputSize, hiddenSize)
	
	gruParams := 0
	for _, p := range gru.GetParams() {
		gruParams += len(p.Data)
	}
	
	lstmParams := 0
	for _, p := range lstm.GetParams() {
		lstmParams += len(p.Data)
	}
	
	if gruParams >= lstmParams {
		t.Error("GRU should have fewer parameters than LSTM")
	}
	
	expectedGRUParams := 3 * hiddenSize * (inputSize + hiddenSize + 2)
	if gruParams != expectedGRUParams {
		t.Errorf("GRU parameter count mismatch: expected %d, got %d", expectedGRUParams, gruParams)
	}
}

func BenchmarkGRUForward(b *testing.B) {
	gru := NewGRU(128, 256)
	input := utils.NewTensor(32, 20, 128)
	
	for i := range input.Data {
		input.Data[i] = float64(i%100) * 0.01
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = gru.Forward(input, false)
		gru.ResetState()
	}
}

func BenchmarkGRUBackward(b *testing.B) {
	gru := NewGRU(128, 256)
	input := utils.NewTensor(16, 10, 128)
	gradOutput := utils.NewTensor(16, 10, 256)
	
	for i := range input.Data {
		input.Data[i] = float64(i%100) * 0.01
	}
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.01
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		gru.ResetState()
		_ = gru.Forward(input, true)
		_ = gru.Backward(gradOutput)
	}
}

func BenchmarkGRUVsLSTMForward(b *testing.B) {
	inputSize := 128
	hiddenSize := 256
	seqLen := 20
	batchSize := 16
	
	gru := NewGRU(inputSize, hiddenSize)
	lstm := NewLSTM(inputSize, hiddenSize)
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%100) * 0.01
	}
	
	b.Run("GRU", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			_ = gru.Forward(input, false)
			gru.ResetState()
		}
	})
	
	b.Run("LSTM", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			_ = lstm.Forward(input, false)
			lstm.ResetState()
		}
	})
}