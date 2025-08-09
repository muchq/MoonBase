package main

import (
	"fmt"
	"log"
	"math/rand"
	"time"
	
	"github.com/MoonBase/go/neuro/activations"
	"github.com/MoonBase/go/neuro/data"
	"github.com/MoonBase/go/neuro/inference"
	"github.com/MoonBase/go/neuro/layers"
	"github.com/MoonBase/go/neuro/loss"
	"github.com/MoonBase/go/neuro/network"
	"github.com/MoonBase/go/neuro/utils"
)

func main() {
	rand.Seed(time.Now().UnixNano())
	
	fmt.Println("=== MNIST with Vendored Data ===")
	fmt.Println("Using embedded MNIST dataset - no download required!")
	fmt.Println()
	
	// Display dataset information
	info := data.GetMNISTInfo()
	fmt.Printf("Dataset Info:\n")
	fmt.Printf("- Training samples: %d\n", info.TrainingSamples)
	fmt.Printf("- Test samples: %d\n", info.TestSamples)
	fmt.Printf("- Image size: %dx%d pixels\n", info.ImageWidth, info.ImageHeight)
	fmt.Printf("- Classes: %v\n", info.Classes)
	fmt.Println()
	
	// Load vendored MNIST data
	// Note: First run will require downloading the files using download_mnist.sh
	fmt.Println("Loading vendored MNIST dataset...")
	
	// Use subset for faster demo (set to 0, 0 for full dataset)
	xTrain, yTrain, xTest, yTest, err := data.LoadVendoredMNISTSubset(5000, 1000)
	if err != nil {
		fmt.Printf("Error: Could not load vendored data: %v\n", err)
		fmt.Println("\nTo vendor the MNIST dataset:")
		fmt.Println("1. cd go/neuro/data/mnist_vendor")
		fmt.Println("2. chmod +x download_mnist.sh")
		fmt.Println("3. ./download_mnist.sh")
		fmt.Println("4. git add *.gz")
		fmt.Println("\nFalling back to synthetic data for demonstration...")
		xTrain, yTrain, xTest, yTest = generateSyntheticData()
	}
	
	fmt.Printf("Loaded: %d training samples, %d test samples\n", 
		xTrain.Shape[0], xTest.Shape[0])
	fmt.Println()
	
	// Build model
	fmt.Println("Building neural network...")
	model := buildModel()
	model.Summary()
	fmt.Println()
	
	// Train
	fmt.Println("Training model...")
	startTime := time.Now()
	trainModel(model, xTrain, yTrain, xTest, yTest)
	trainTime := time.Since(startTime)
	fmt.Printf("Total training time: %v\n", trainTime)
	fmt.Println()
	
	// Save model
	modelDir := "models/mnist_vendored_model"
	fmt.Printf("Saving model to %s...\n", modelDir)
	err = inference.SaveModel(model, modelDir)
	if err != nil {
		log.Printf("Warning: Failed to save model: %v\n", err)
	}
	
	// Test inference
	fmt.Println("\n=== Testing Inference ===")
	testInference(model, xTest, yTest)
	
	fmt.Println("\n=== Complete! ===")
	fmt.Println("The model has been trained on vendored MNIST data.")
	fmt.Println("No external downloads were required!")
}

func buildModel() *network.Model {
	model := network.NewModel()
	
	// Simple but effective architecture for MNIST
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
	epochs := 5 // Fewer epochs for quick demo
	batchSize := 32
	
	dataset := data.NewDataset(xTrain, yTrain, batchSize, true)
	
	for epoch := 0; epoch < epochs; epoch++ {
		dataset.Reset()
		totalLoss := 0.0
		batches := 0
		
		// Progress indicator
		fmt.Printf("Epoch %d/%d: ", epoch+1, epochs)
		
		for dataset.HasNext() {
			xBatch, yBatch := dataset.NextBatch()
			loss := model.Train(xBatch, yBatch)
			totalLoss += loss
			batches++
			
			// Show progress dots
			if batches%20 == 0 {
				fmt.Print(".")
			}
		}
		
		avgLoss := totalLoss / float64(batches)
		_, testAccuracy := model.Evaluate(xTest, yTest)
		
		fmt.Printf(" Loss: %.4f, Accuracy: %.2f%%\n", avgLoss, testAccuracy*100)
	}
}

func testInference(model *network.Model, xTest, yTest *utils.Tensor) {
	fmt.Println("Testing on 5 random samples:")
	
	indices := rand.Perm(xTest.Shape[0])[:5]
	correct := 0
	
	for i, idx := range indices {
		// Get sample
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
		
		// Predict
		output := model.Predict(sample)
		
		// Find predicted class
		predictedLabel := 0
		maxProb := output.Data[0]
		for j := 1; j < len(output.Data); j++ {
			if output.Data[j] > maxProb {
				maxProb = output.Data[j]
				predictedLabel = j
			}
		}
		
		isCorrect := predictedLabel == trueLabel
		if isCorrect {
			correct++
			fmt.Printf("✓ ")
		} else {
			fmt.Printf("✗ ")
		}
		
		fmt.Printf("Sample %d: True=%d, Predicted=%d (%.1f%% confidence)\n",
			i+1, trueLabel, predictedLabel, maxProb*100)
	}
	
	fmt.Printf("\nAccuracy on samples: %d/5 (%.0f%%)\n", 
		correct, float64(correct)/5.0*100)
}

func generateSyntheticData() (*utils.Tensor, *utils.Tensor, *utils.Tensor, *utils.Tensor) {
	// Create synthetic data that has some structure
	trainSize := 1000
	testSize := 200
	
	xTrain := utils.NewTensor(trainSize, 784)
	xTest := utils.NewTensor(testSize, 784)
	
	trainLabels := make([]int, trainSize)
	testLabels := make([]int, testSize)
	
	// Generate data with patterns based on class
	for i := 0; i < trainSize; i++ {
		label := i % 10
		trainLabels[i] = label
		
		// Create a pattern for each digit
		for j := 0; j < 784; j++ {
			if j >= label*78 && j < (label+1)*78 {
				xTrain.Set(rand.Float64()*0.5+0.5, i, j)
			} else {
				xTrain.Set(rand.Float64()*0.2, i, j)
			}
		}
	}
	
	for i := 0; i < testSize; i++ {
		label := i % 10
		testLabels[i] = label
		
		for j := 0; j < 784; j++ {
			if j >= label*78 && j < (label+1)*78 {
				xTest.Set(rand.Float64()*0.5+0.5, i, j)
			} else {
				xTest.Set(rand.Float64()*0.2, i, j)
			}
		}
	}
	
	// Normalize
	xTrain, _, _ = data.Normalize(xTrain)
	xTest, _, _ = data.Normalize(xTest)
	
	// One-hot encode labels
	yTrain := data.OneHotEncode(trainLabels, 10)
	yTest := data.OneHotEncode(testLabels, 10)
	
	return xTrain, yTrain, xTest, yTest
}