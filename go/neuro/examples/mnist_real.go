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
	
	fmt.Println("=== MNIST Handwritten Digit Recognition ===")
	fmt.Println()
	
	// Load real MNIST data from vendored files
	// Using a subset for faster demo (5000 train, 1000 test)
	// Set to 0, 0 to use full dataset (60000 train, 10000 test)
	fmt.Println("Loading MNIST dataset from vendored files...")
	xTrain, yTrain, xTest, yTest, err := data.LoadVendoredMNISTSubset(5000, 1000)
	if err != nil {
		// Fall back to synthetic data if vendored files not found
		fmt.Println("Failed to load vendored MNIST data, using synthetic data instead:", err)
		xTrain, yTrain, xTest, yTest = generateSyntheticData()
	}
	
	fmt.Printf("Training data shape: (%d, %d)\n", xTrain.Shape[0], xTrain.Shape[1])
	fmt.Printf("Test data shape: (%d, %d)\n", xTest.Shape[0], xTest.Shape[1])
	fmt.Println()
	
	// Build the model
	fmt.Println("Building neural network model...")
	model := buildModel()
	model.Summary()
	fmt.Println()
	
	// Train the model
	fmt.Println("Training the model...")
	trainModel(model, xTrain, yTrain, xTest, yTest)
	fmt.Println()
	
	// Save the trained model
	modelDir := "models/mnist_real_model"
	fmt.Println("Saving the trained model...")
	err = inference.SaveModel(model, modelDir)
	if err != nil {
		log.Fatalf("Failed to save model: %v", err)
	}
	fmt.Printf("Model saved to %s\n", modelDir)
	fmt.Println()
	
	// Load model for inference
	fmt.Println("Loading model for inference...")
	engine, err := inference.LoadModelForInference(modelDir)
	if err != nil {
		log.Fatalf("Failed to load model for inference: %v", err)
	}
	fmt.Println("Model loaded successfully")
	fmt.Println()
	
	// Perform inference and show results
	fmt.Println("Testing inference on sample digits...")
	performDetailedInference(engine, xTest, yTest)
	
	// Demonstrate real-world usage
	fmt.Println("\n=== Real-World Usage Example ===")
	demonstrateRealWorldUsage(engine)
	
	fmt.Println("\n=== Training completed successfully! ===")
}

func buildModel() *network.Model {
	model := network.NewModel()
	
	// Architecture optimized for MNIST
	// Input: 784 features (28x28 flattened image)
	// Hidden layers with dropout for regularization
	// Output: 10 classes (digits 0-9)
	
	model.Add(layers.NewDense(784, 128, activations.NewReLU()))
	model.Add(layers.NewDropout(0.2))
	model.Add(layers.NewDense(128, 64, activations.NewReLU()))
	model.Add(layers.NewDropout(0.2))
	model.Add(layers.NewDense(64, 10, activations.NewSoftmax()))
	
	model.SetLoss(loss.NewCategoricalCrossEntropy())
	model.SetOptimizer(network.NewAdam(0.001))
	
	return model
}

func trainModel(model *network.Model, xTrain, yTrain, xTest, yTest *utils.Tensor) {
	epochs := 10
	batchSize := 32
	
	dataset := data.NewDataset(xTrain, yTrain, batchSize, true)
	
	fmt.Printf("Training for %d epochs with batch size %d\n", epochs, batchSize)
	fmt.Println("Progress:")
	
	bestAccuracy := 0.0
	
	for epoch := 0; epoch < epochs; epoch++ {
		dataset.Reset()
		totalLoss := 0.0
		batches := 0
		
		startTime := time.Now()
		
		for dataset.HasNext() {
			xBatch, yBatch := dataset.NextBatch()
			loss := model.Train(xBatch, yBatch)
			totalLoss += loss
			batches++
			
			if batches%50 == 0 {
				fmt.Printf(".")
			}
		}
		
		avgLoss := totalLoss / float64(batches)
		_, testAccuracy := model.Evaluate(xTest, yTest)
		
		epochTime := time.Since(startTime)
		
		fmt.Printf("\nEpoch %d/%d - Time: %v - Loss: %.4f - Test Accuracy: %.2f%%\n",
			epoch+1, epochs, epochTime, avgLoss, testAccuracy*100)
		
		if testAccuracy > bestAccuracy {
			bestAccuracy = testAccuracy
			fmt.Printf("  🎯 New best accuracy!\n")
		}
	}
	
	fmt.Printf("\nBest test accuracy achieved: %.2f%%\n", bestAccuracy*100)
}

func performDetailedInference(engine *inference.InferenceEngine, xTest, yTest *utils.Tensor) {
	fmt.Println("\nAnalyzing 10 random test samples:")
	fmt.Println("==================================================")
	
	correct := 0
	numSamples := 10
	
	// Select random indices
	indices := rand.Perm(xTest.Shape[0])[:numSamples]
	
	for i, idx := range indices {
		// Extract single sample
		sample := utils.NewTensor(1, xTest.Shape[1])
		for j := 0; j < xTest.Shape[1]; j++ {
			sample.Set(xTest.Get(idx, j), 0, j)
		}
		
		// Get true label
		trueLabel := -1
		for j := 0; j < yTest.Shape[1]; j++ {
			if yTest.Get(idx, j) > 0.5 {
				trueLabel = j
				break
			}
		}
		
		// Make prediction
		classIdx, confidence, err := engine.PredictClass(sample)
		if err != nil {
			fmt.Printf("Sample %d: Error - %v\n", i+1, err)
			continue
		}
		
		isCorrect := classIdx == trueLabel
		if isCorrect {
			correct++
			fmt.Printf("✓ ")
		} else {
			fmt.Printf("✗ ")
		}
		
		fmt.Printf("Sample %d: True=%d, Predicted=%d (%.1f%% confidence)\n",
			i+1, trueLabel, classIdx, confidence*100)
		
		// Show top 3 predictions for interesting cases
		if !isCorrect || confidence < 0.8 {
			indices, values, _ := engine.PredictTopK(sample, 3)
			fmt.Printf("   Top predictions: ")
			for j := 0; j < len(indices); j++ {
				fmt.Printf("%d(%.1f%%) ", indices[j], values[j]*100)
			}
			fmt.Println()
		}
	}
	
	fmt.Printf("\nAccuracy on these samples: %d/%d (%.1f%%)\n", 
		correct, numSamples, float64(correct)/float64(numSamples)*100)
}

func demonstrateRealWorldUsage(engine *inference.InferenceEngine) {
	fmt.Println("\nSimulating a digit recognition service...")
	
	// Create a mock "unknown" digit (random noise)
	unknownDigit := utils.RandomTensor(1, 784)
	for i := range unknownDigit.Data {
		unknownDigit.Data[i] = (unknownDigit.Data[i] + 1) / 2
	}
	
	// Normalize as we would with real data
	unknownDigit, _, _ = data.Normalize(unknownDigit)
	
	fmt.Println("Processing unknown digit image...")
	
	// Get prediction with confidence
	classIdx, confidence, err := engine.PredictClass(unknownDigit)
	if err != nil {
		fmt.Printf("Error processing image: %v\n", err)
		return
	}
	
	// Typical confidence threshold for production
	confidenceThreshold := 0.7
	
	if confidence > confidenceThreshold {
		fmt.Printf("Recognized digit: %d (confidence: %.1f%%)\n", 
			classIdx, confidence*100)
	} else {
		fmt.Printf("Low confidence prediction: %d (only %.1f%% confident)\n",
			classIdx, confidence*100)
		fmt.Println("Image may be unclear or not a digit")
		
		// Show alternatives
		indices, values, _ := engine.PredictTopK(unknownDigit, 3)
		fmt.Print("Possible digits: ")
		for i := 0; i < len(indices); i++ {
			fmt.Printf("%d(%.1f%%) ", indices[i], values[i]*100)
		}
		fmt.Println()
	}
	
	// Batch processing example
	fmt.Println("\nBatch processing demonstration:")
	batchSize := 100
	inputs := make([]*utils.Tensor, batchSize)
	for i := 0; i < batchSize; i++ {
		inputs[i] = utils.RandomTensor(1, 784)
		inputs[i], _, _ = data.Normalize(inputs[i])
	}
	
	start := time.Now()
	outputs, err := engine.PredictBatch(inputs)
	elapsed := time.Since(start)
	
	if err != nil {
		fmt.Printf("Batch processing error: %v\n", err)
		return
	}
	
	fmt.Printf("Processed %d images in %v\n", len(outputs), elapsed)
	fmt.Printf("Average time per image: %v\n", elapsed/time.Duration(batchSize))
	fmt.Printf("Throughput: %.0f images/second\n", 
		float64(batchSize)/elapsed.Seconds())
}

func generateSyntheticData() (*utils.Tensor, *utils.Tensor, *utils.Tensor, *utils.Tensor) {
	fmt.Println("Generating synthetic data for demonstration...")
	
	numTrain := 1000
	numTest := 200
	inputSize := 784
	numClasses := 10
	
	xTrain := utils.RandomTensor(numTrain, inputSize)
	xTest := utils.RandomTensor(numTest, inputSize)
	
	// Create somewhat structured synthetic data
	for i := 0; i < numTrain; i++ {
		label := i % numClasses
		// Add some pattern based on label
		for j := label * 78; j < (label+1)*78 && j < inputSize; j++ {
			xTrain.Set(xTrain.Get(i, j)+0.5, i, j)
		}
	}
	
	for i := 0; i < numTest; i++ {
		label := i % numClasses
		for j := label * 78; j < (label+1)*78 && j < inputSize; j++ {
			xTest.Set(xTest.Get(i, j)+0.5, i, j)
		}
	}
	
	// Normalize
	xTrain, _, _ = data.Normalize(xTrain)
	xTest, _, _ = data.Normalize(xTest)
	
	// Create labels
	trainLabels := make([]int, numTrain)
	testLabels := make([]int, numTest)
	for i := range trainLabels {
		trainLabels[i] = i % numClasses
	}
	for i := range testLabels {
		testLabels[i] = i % numClasses
	}
	
	yTrain := data.OneHotEncode(trainLabels, numClasses)
	yTest := data.OneHotEncode(testLabels, numClasses)
	
	return xTrain, yTrain, xTest, yTest
}