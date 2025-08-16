package main

import (
	"fmt"
	"log"
	"math/rand"
	"time"
	
	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/data"
	"github.com/muchq/moonbase/go/neuro/inference"
	"github.com/muchq/moonbase/go/neuro/layers"
	"github.com/muchq/moonbase/go/neuro/loss"
	"github.com/muchq/moonbase/go/neuro/network"
	"github.com/muchq/moonbase/go/neuro/utils"
)

func main() {
	rand.Seed(time.Now().UnixNano())
	
	fmt.Println("=== Neuro Neural Network Framework - Complete Example ===")
	fmt.Println()
	
	fmt.Println("Step 1: Generate synthetic MNIST-like data")
	xTrain, yTrain, xTest, yTest := generateSyntheticData()
	fmt.Printf("Training data shape: (%d, %d)\n", xTrain.Shape[0], xTrain.Shape[1])
	fmt.Printf("Test data shape: (%d, %d)\n", xTest.Shape[0], xTest.Shape[1])
	fmt.Println()
	
	fmt.Println("Step 2: Build and configure the model")
	model := buildModel()
	model.Summary()
	fmt.Println()
	
	fmt.Println("Step 3: Train the model")
	trainModel(model, xTrain, yTrain, xTest, yTest)
	fmt.Println()
	
	fmt.Println("Step 4: Save the trained model")
	modelDir := "models/mnist_model"
	err := inference.SaveModel(model, modelDir)
	if err != nil {
		log.Fatalf("Failed to save model: %v", err)
	}
	fmt.Printf("Model saved to %s\n", modelDir)
	fmt.Println()
	
	fmt.Println("Step 5: Load model for inference")
	engine, err := inference.LoadModelForInference(modelDir)
	if err != nil {
		log.Fatalf("Failed to load model for inference: %v", err)
	}
	fmt.Println("Model loaded successfully")
	fmt.Println()
	
	fmt.Println("Step 6: Perform inference on test samples")
	performInference(engine, xTest)
	fmt.Println()
	
	fmt.Println("Step 7: Demonstrate batch inference")
	performBatchInference(engine, xTest)
	fmt.Println()
	
	fmt.Println("Step 8: Demonstrate model warmup")
	demonstrateWarmup(engine)
	
	fmt.Println("\n=== Example completed successfully! ===")
}

func generateSyntheticData() (*utils.Tensor, *utils.Tensor, *utils.Tensor, *utils.Tensor) {
	numSamples := 1000
	inputSize := 784
	numClasses := 10
	
	xTrain := utils.RandomTensor(numSamples, inputSize)
	for i := range xTrain.Data {
		xTrain.Data[i] = (xTrain.Data[i] + 1) / 2
	}
	
	yTrainLabels := make([]float64, numSamples)
	for i := range yTrainLabels {
		yTrainLabels[i] = float64(rand.Intn(numClasses))
	}
	
	yTrain := oneHotEncode(yTrainLabels, numClasses)
	
	testSamples := 200
	xTest := utils.RandomTensor(testSamples, inputSize)
	for i := range xTest.Data {
		xTest.Data[i] = (xTest.Data[i] + 1) / 2
	}
	
	yTestLabels := make([]float64, testSamples)
	for i := range yTestLabels {
		yTestLabels[i] = float64(rand.Intn(numClasses))
	}
	
	yTest := oneHotEncode(yTestLabels, numClasses)
	
	xTrain, _, _ = normalizeData(xTrain)
	xTest, _, _ = normalizeData(xTest)
	
	return xTrain, yTrain, xTest, yTest
}

func oneHotEncode(labels []float64, numClasses int) *utils.Tensor {
	numSamples := len(labels)
	encoded := utils.NewTensor(numSamples, numClasses)
	
	for i, label := range labels {
		classIdx := int(label)
		encoded.Set(1.0, i, classIdx)
	}
	
	return encoded
}

func normalizeData(x *utils.Tensor) (*utils.Tensor, float64, float64) {
	mean := x.Mean()
	
	variance := 0.0
	for _, val := range x.Data {
		diff := val - mean
		variance += diff * diff
	}
	variance /= float64(len(x.Data))
	std := utils.Sqrt(variance)
	
	normalized := x.Copy()
	for i := range normalized.Data {
		normalized.Data[i] = (normalized.Data[i] - mean) / (std + 1e-8)
	}
	
	return normalized, mean, std
}

func buildModel() *network.Model {
	model := network.NewModel()
	
	model.Add(layers.NewDense(784, 256, activations.NewReLU()))
	model.Add(layers.NewDropout(0.2))
	model.Add(layers.NewBatchNorm(256))
	model.Add(layers.NewDense(256, 128, activations.NewReLU()))
	model.Add(layers.NewDropout(0.2))
	model.Add(layers.NewDense(128, 64, activations.NewReLU()))
	model.Add(layers.NewDense(64, 10, activations.NewSoftmax()))
	
	model.SetLoss(loss.NewCategoricalCrossEntropy())
	model.SetOptimizer(network.NewAdam(0.001))
	
	return model
}

func trainModel(model *network.Model, xTrain, yTrain, xTest, yTest *utils.Tensor) {
	epochs := 10
	batchSize := 32
	
	dataset := data.NewDataset(xTrain, yTrain, batchSize, true)
	
	fmt.Println("Starting training...")
	fmt.Println("Epochs:", epochs)
	fmt.Println("Batch size:", batchSize)
	fmt.Println()
	
	checkpointMgr := inference.NewCheckpointManager("checkpoints", 3)
	
	for epoch := 0; epoch < epochs; epoch++ {
		dataset.Reset()
		totalLoss := 0.0
		batches := 0
		
		for dataset.HasNext() {
			xBatch, yBatch := dataset.NextBatch()
			loss := model.Train(xBatch, yBatch)
			totalLoss += loss
			batches++
			
			if batches%10 == 0 {
				fmt.Printf(".")
			}
		}
		
		avgLoss := totalLoss / float64(batches)
		
		testLoss, testAccuracy := model.Evaluate(xTest, yTest)
		
		fmt.Printf("\nEpoch %d/%d - Loss: %.4f, Test Loss: %.4f, Test Accuracy: %.2f%%\n",
			epoch+1, epochs, avgLoss, testLoss, testAccuracy*100)
		
		err := checkpointMgr.SaveCheckpoint(model, epoch+1, avgLoss)
		if err != nil {
			fmt.Printf("Warning: Failed to save checkpoint: %v\n", err)
		}
	}
	
	fmt.Println("\nTraining completed!")
}

func performInference(engine *inference.InferenceEngine, xTest *utils.Tensor) {
	fmt.Println("Performing inference on 5 test samples:")
	fmt.Println()
	
	for i := 0; i < 5 && i < xTest.Shape[0]; i++ {
		sample := utils.NewTensor(1, xTest.Shape[1])
		for j := 0; j < xTest.Shape[1]; j++ {
			sample.Set(xTest.Get(i, j), 0, j)
		}
		
		classIdx, confidence, err := engine.PredictClass(sample)
		if err != nil {
			fmt.Printf("Sample %d: Error - %v\n", i+1, err)
			continue
		}
		
		fmt.Printf("Sample %d: Predicted class %d with %.2f%% confidence\n",
			i+1, classIdx, confidence*100)
		
		indices, values, err := engine.PredictTopK(sample, 3)
		if err != nil {
			continue
		}
		
		fmt.Printf("  Top 3 predictions:\n")
		for j := 0; j < len(indices); j++ {
			fmt.Printf("    - Class %d: %.2f%%\n", indices[j], values[j]*100)
		}
		fmt.Println()
	}
}

func performBatchInference(engine *inference.InferenceEngine, xTest *utils.Tensor) {
	fmt.Println("Performing batch inference on 10 samples:")
	
	batchSize := 10
	if batchSize > xTest.Shape[0] {
		batchSize = xTest.Shape[0]
	}
	
	inputs := make([]*utils.Tensor, batchSize)
	for i := 0; i < batchSize; i++ {
		sample := utils.NewTensor(1, xTest.Shape[1])
		for j := 0; j < xTest.Shape[1]; j++ {
			sample.Set(xTest.Get(i, j), 0, j)
		}
		inputs[i] = sample
	}
	
	start := time.Now()
	outputs, err := engine.PredictBatch(inputs)
	elapsed := time.Since(start)
	
	if err != nil {
		fmt.Printf("Batch inference error: %v\n", err)
		return
	}
	
	fmt.Printf("Batch inference completed in %v\n", elapsed)
	fmt.Printf("Average time per sample: %v\n", elapsed/time.Duration(batchSize))
	fmt.Printf("Processed %d samples successfully\n", len(outputs))
}

func demonstrateWarmup(engine *inference.InferenceEngine) {
	fmt.Println("Demonstrating model warmup:")
	
	modelInfo := engine.GetModelInfo()
	fmt.Printf("Model expects input shape: %v\n", modelInfo.InputShape)
	
	fmt.Println("Running warmup with 5 iterations...")
	err := engine.Warmup(5)
	if err != nil {
		fmt.Printf("Warmup failed: %v\n", err)
		return
	}
	
	fmt.Println("Warmup completed successfully!")
	
	testInput := utils.RandomTensor(modelInfo.InputShape...)
	
	timings := make([]time.Duration, 10)
	for i := 0; i < 10; i++ {
		start := time.Now()
		_, err := engine.Predict(testInput)
		timings[i] = time.Since(start)
		if err != nil {
			fmt.Printf("Prediction %d failed: %v\n", i+1, err)
			continue
		}
	}
	
	var totalTime time.Duration
	for _, t := range timings {
		totalTime += t
	}
	avgTime := totalTime / time.Duration(len(timings))
	
	fmt.Printf("Average inference time after warmup: %v\n", avgTime)
}