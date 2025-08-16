package network

import (
	"math"
	"testing"
	
	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/data"
	"github.com/muchq/moonbase/go/neuro/layers"
	"github.com/muchq/moonbase/go/neuro/loss"
	"github.com/muchq/moonbase/go/neuro/utils"
)

func TestModelForwardPass(t *testing.T) {
	model := NewModel()
	model.Add(layers.NewDense(10, 20, activations.NewReLU()))
	model.Add(layers.NewDense(20, 5, nil))
	
	input := utils.RandomTensor(4, 10)
	output := model.Forward(input, false)
	
	if output.Shape[0] != 4 || output.Shape[1] != 5 {
		t.Errorf("Expected output shape [4,5], got %v", output.Shape)
	}
}

func TestModelTraining(t *testing.T) {
	model := NewModel()
	model.Add(layers.NewDense(2, 4, activations.NewReLU()))
	model.Add(layers.NewDense(4, 1, activations.NewSigmoid()))
	model.SetLoss(loss.NewMSE())
	model.SetOptimizer(NewSGD(0.1, 0.0))
	
	x := utils.NewTensorFromData([]float64{
		0, 0,
		0, 1,
		1, 0,
		1, 1,
	}, 4, 2)
	
	y := utils.NewTensorFromData([]float64{0, 1, 1, 0}, 4, 1)
	
	initialLoss := model.Train(x, y)
	
	for i := 0; i < 100; i++ {
		model.Train(x, y)
	}
	
	finalLoss := model.Train(x, y)
	
	if finalLoss >= initialLoss {
		t.Errorf("Model did not improve: initial loss %f, final loss %f", initialLoss, finalLoss)
	}
}

func TestModelEvaluation(t *testing.T) {
	model := NewModel()
	model.Add(layers.NewDense(10, 20, activations.NewReLU()))
	model.Add(layers.NewDense(20, 3, activations.NewSoftmax()))
	model.SetLoss(loss.NewCategoricalCrossEntropy())
	
	x := utils.RandomTensor(5, 10)
	y := data.OneHotEncode([]int{0, 1, 2, 1, 0}, 3)
	
	lossVal, accuracy := model.Evaluate(x, y)
	
	if math.IsNaN(lossVal) || math.IsInf(lossVal, 0) {
		t.Errorf("Invalid loss value: %f", lossVal)
	}
	
	if accuracy < 0 || accuracy > 1 {
		t.Errorf("Invalid accuracy: %f", accuracy)
	}
}

func TestOptimizers(t *testing.T) {
	testCases := []struct {
		name      string
		optimizer Optimizer
	}{
		{"SGD", NewSGD(0.01, 0.0)},
		{"Adam", NewAdam(0.001)},
		{"RMSprop", NewRMSprop(0.001)},
	}
	
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			model := NewModel()
			model.Add(layers.NewDense(5, 10, activations.NewReLU()))
			model.Add(layers.NewDense(10, 2, nil))
			model.SetLoss(loss.NewMSE())
			model.SetOptimizer(tc.optimizer)
			
			x := utils.RandomTensor(10, 5)
			y := utils.RandomTensor(10, 2)
			
			initialLoss := model.Train(x, y)
			
			for i := 0; i < 50; i++ {
				model.Train(x, y)
			}
			
			finalLoss := model.Train(x, y)
			
			if finalLoss >= initialLoss {
				t.Errorf("%s: Model did not improve: initial loss %f, final loss %f", 
					tc.name, initialLoss, finalLoss)
			}
		})
	}
}

func TestBatchNormLayer(t *testing.T) {
	bn := layers.NewBatchNorm(10)
	
	input := utils.RandomTensor(32, 10)
	
	outputTrain := bn.Forward(input, true)
	if outputTrain.Shape[0] != 32 || outputTrain.Shape[1] != 10 {
		t.Errorf("Expected output shape [32,10], got %v", outputTrain.Shape)
	}
	
	outputEval := bn.Forward(input, false)
	if outputEval.Shape[0] != 32 || outputEval.Shape[1] != 10 {
		t.Errorf("Expected output shape [32,10], got %v", outputEval.Shape)
	}
	
	grad := utils.RandomTensor(32, 10)
	gradInput := bn.Backward(grad)
	
	if gradInput.Shape[0] != 32 || gradInput.Shape[1] != 10 {
		t.Errorf("Expected gradient shape [32,10], got %v", gradInput.Shape)
	}
}

func TestDropoutLayer(t *testing.T) {
	dropout := layers.NewDropout(0.5)
	
	input := utils.NewTensor(100)
	for i := range input.Data {
		input.Data[i] = 1.0
	}
	
	outputTrain := dropout.Forward(input, true)
	
	zeros := 0
	for _, v := range outputTrain.Data {
		if v == 0 {
			zeros++
		}
	}
	
	if zeros == 0 {
		t.Error("Dropout should have zeroed some values during training")
	}
	
	outputEval := dropout.Forward(input, false)
	for i, v := range outputEval.Data {
		if v != input.Data[i] {
			t.Errorf("Dropout should not modify values during evaluation")
			break
		}
	}
}