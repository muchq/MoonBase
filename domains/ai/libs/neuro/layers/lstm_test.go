package layers

import (
	"math"
	"testing"
	
	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
)

func TestLSTMForward(t *testing.T) {
	inputSize := 10
	hiddenSize := 20
	seqLen := 5
	batchSize := 2
	
	lstm := NewLSTM(inputSize, hiddenSize)
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%10) * 0.1
	}
	
	output := lstm.Forward(input, true)
	
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
			t.Error("LSTM output should be in range [-1, 1] due to output gate and tanh")
			break
		}
	}
}

func TestLSTMBackward(t *testing.T) {
	inputSize := 5
	hiddenSize := 8
	seqLen := 3
	batchSize := 1
	
	lstm := NewLSTM(inputSize, hiddenSize)
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%5) * 0.2 - 0.5
	}
	
	output := lstm.Forward(input, true)
	
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 0.1
	}
	
	gradInput := lstm.Backward(gradOutput)
	
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
	
	if lstm.GradWeightIH == nil || lstm.GradWeightHH == nil {
		t.Error("Weight gradients not computed")
	}
	if lstm.GradBiasIH == nil || lstm.GradBiasHH == nil {
		t.Error("Bias gradients not computed")
	}
	
	if lstm.GradWeightIH.Shape[0] != inputSize || lstm.GradWeightIH.Shape[1] != 4*hiddenSize {
		t.Errorf("GradWeightIH has wrong shape: %v", lstm.GradWeightIH.Shape)
	}
	if lstm.GradWeightHH.Shape[0] != hiddenSize || lstm.GradWeightHH.Shape[1] != 4*hiddenSize {
		t.Errorf("GradWeightHH has wrong shape: %v", lstm.GradWeightHH.Shape)
	}
}

func TestLSTMGradientCheck(t *testing.T) {
	inputSize := 3
	hiddenSize := 4
	seqLen := 2
	batchSize := 1
	epsilon := 1e-5
	tolerance := 1e-3
	
	lstm := NewLSTM(inputSize, hiddenSize)
	
	input := utils.NewTensor(batchSize, seqLen, inputSize)
	for i := range input.Data {
		input.Data[i] = float64(i%3) * 0.3 - 0.4
	}
	
	output := lstm.Forward(input, true)
	loss := 0.0
	for _, v := range output.Data {
		loss += v * v
	}
	loss /= float64(len(output.Data))
	
	gradOutput := output.Copy()
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 2.0 * gradOutput.Data[i] / float64(len(gradOutput.Data))
	}
	
	_ = lstm.Backward(gradOutput)
	analyticalGrad := lstm.GradWeightIH.Data[0]
	
	originalWeight := lstm.WeightIH.Data[0]
	
	lstm.WeightIH.Data[0] = originalWeight + epsilon
	lstm.ResetState()
	outputPlus := lstm.Forward(input, false)
	lossPlus := 0.0
	for _, v := range outputPlus.Data {
		lossPlus += v * v
	}
	lossPlus /= float64(len(outputPlus.Data))
	
	lstm.WeightIH.Data[0] = originalWeight - epsilon
	lstm.ResetState()
	outputMinus := lstm.Forward(input, false)
	lossMinus := 0.0
	for _, v := range outputMinus.Data {
		lossMinus += v * v
	}
	lossMinus /= float64(len(outputMinus.Data))
	
	numericalGrad := (lossPlus - lossMinus) / (2 * epsilon)
	
	lstm.WeightIH.Data[0] = originalWeight
	
	relError := math.Abs(analyticalGrad-numericalGrad) / (math.Abs(analyticalGrad) + math.Abs(numericalGrad) + 1e-8)
	
	if relError > tolerance {
		t.Errorf("Gradient check failed: analytical=%f, numerical=%f, relative error=%f", 
			analyticalGrad, numericalGrad, relError)
	}
}

func TestLSTMStateReset(t *testing.T) {
	lstm := NewLSTM(5, 10)
	
	input := utils.NewTensor(1, 3, 5)
	for i := range input.Data {
		input.Data[i] = float64(i) * 0.1
	}
	
	_ = lstm.Forward(input, false)
	
	if lstm.HiddenState == nil {
		t.Error("Hidden state should be set after forward pass")
	}
	if lstm.CellState == nil {
		t.Error("Cell state should be set after forward pass")
	}
	
	lstm.ResetState()
	
	if lstm.HiddenState != nil {
		t.Error("Hidden state should be nil after reset")
	}
	if lstm.CellState != nil {
		t.Error("Cell state should be nil after reset")
	}
}

func TestLSTMMemoryCellBehavior(t *testing.T) {
	lstm := NewLSTM(5, 10)
	
	inputConstant := utils.NewTensor(1, 5, 5)
	for i := range inputConstant.Data {
		inputConstant.Data[i] = 0.5
	}
	
	inputZero := utils.NewTensor(1, 5, 5)
	
	output1 := lstm.Forward(inputConstant, false)
	prevOutput := output1.Copy()
	
	output2 := lstm.Forward(inputZero, false)
	
	decayed := false
	for i := range output2.Data {
		if math.Abs(output2.Data[i]) < math.Abs(prevOutput.Data[i]) {
			decayed = true
			break
		}
	}
	
	if !decayed {
		t.Error("LSTM should show memory decay with zero input due to forget gate")
	}
}

func TestLSTMForgetGateBias(t *testing.T) {
	lstm := NewLSTM(5, 10)
	
	// Forget gate is at indices 0 to hiddenSize
	hiddenSize := 10
	forgetBiasStart := 0
	forgetBiasEnd := hiddenSize
	for i := forgetBiasStart; i < forgetBiasEnd; i++ {
		if lstm.BiasIH.Data[i] != 1.0 || lstm.BiasHH.Data[i] != 1.0 {
			t.Errorf("Forget gate bias at index %d should be 1.0, got IH=%f, HH=%f", 
				i, lstm.BiasIH.Data[i], lstm.BiasHH.Data[i])
		}
	}
	
	// Verify other gates are initialized to 0
	for i := hiddenSize; i < 4*hiddenSize; i++ {
		if lstm.BiasIH.Data[i] != 0.0 || lstm.BiasHH.Data[i] != 0.0 {
			t.Errorf("Non-forget gate bias at index %d should be 0.0, got IH=%f, HH=%f",
				i, lstm.BiasIH.Data[i], lstm.BiasHH.Data[i])
		}
	}
}

func TestLSTMVariableSequenceLength(t *testing.T) {
	lstm := NewLSTM(8, 16)
	
	for seqLen := 1; seqLen <= 10; seqLen++ {
		input := utils.NewTensor(2, seqLen, 8)
		for i := range input.Data {
			input.Data[i] = float64(i%10) * 0.05
		}
		
		output := lstm.Forward(input, false)
		
		if output.Shape[1] != seqLen {
			t.Errorf("For seq_len=%d, expected output seq_len=%d, got %d", 
				seqLen, seqLen, output.Shape[1])
		}
		
		lstm.ResetState()
	}
}

func TestLSTMGradientClipping(t *testing.T) {
	lstm := NewLSTM(5, 10)
	lstm.GradientClipValue = 1.0
	
	input := utils.NewTensor(1, 5, 5)
	for i := range input.Data {
		input.Data[i] = float64(i) * 10.0
	}
	
	output := lstm.Forward(input, true)
	
	gradOutput := utils.NewTensor(output.Shape...)
	for i := range gradOutput.Data {
		gradOutput.Data[i] = 100.0
	}
	
	_ = lstm.Backward(gradOutput)
	
	maxGrad := 0.0
	for _, grad := range lstm.GetGradients() {
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

func BenchmarkLSTMForward(b *testing.B) {
	lstm := NewLSTM(128, 256)
	input := utils.NewTensor(32, 20, 128)
	
	for i := range input.Data {
		input.Data[i] = float64(i%100) * 0.01
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = lstm.Forward(input, false)
		lstm.ResetState()
	}
}

func BenchmarkLSTMBackward(b *testing.B) {
	lstm := NewLSTM(128, 256)
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
		lstm.ResetState()
		_ = lstm.Forward(input, true)
		_ = lstm.Backward(gradOutput)
	}
}