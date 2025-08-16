package main

import (
	"fmt"
	"math"
	"math/rand"
	"time"

	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/data"
	"github.com/muchq/moonbase/go/neuro/layers"
	"github.com/muchq/moonbase/go/neuro/utils"
)


// SimpleCNN represents a simple CNN model for MNIST
type SimpleCNN struct {
	conv1    *layers.Conv2D
	pool1    *layers.MaxPool2D
	conv2    *layers.Conv2D
	pool2    *layers.MaxPool2D
	flatten  *layers.Flatten
	dense1   *layers.Dense
	dense2   *layers.Dense
	dropout1 *layers.Dropout
	dropout2 *layers.Dropout
}

// NewSimpleCNN creates a new CNN model
func NewSimpleCNN() *SimpleCNN {
	return &SimpleCNN{
		// First convolutional block
		conv1: layers.NewConv2D(1, 32, []int{3, 3}, 1, "valid", true),
		pool1: layers.NewMaxPool2D([]int{2, 2}, 2, "valid"),
		
		// Second convolutional block
		conv2: layers.NewConv2D(32, 64, []int{3, 3}, 1, "valid", true),
		pool2: layers.NewMaxPool2D([]int{2, 2}, 2, "valid"),
		
		// Flatten and dense layers
		flatten: layers.NewFlatten(),
		dense1:  layers.NewDense(1600, 128, &activations.ReLU{}), // 5*5*64 = 1600
		dense2:  layers.NewDense(128, 10, &activations.Softmax{}),
		
		// Dropout for regularization
		dropout1: layers.NewDropout(0.25),
		dropout2: layers.NewDropout(0.5),
	}
}

// Forward performs forward propagation
func (m *SimpleCNN) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	// Conv block 1
	x := m.conv1.Forward(input, training)
	x = relu(x)
	x = m.pool1.Forward(x, training)
	x = m.dropout1.Forward(x, training)
	
	// Conv block 2
	x = m.conv2.Forward(x, training)
	x = relu(x)
	x = m.pool2.Forward(x, training)
	x = m.dropout2.Forward(x, training)
	
	// Dense layers (ReLU is built into dense1, Softmax is built into dense2)
	x = m.flatten.Forward(x, training)
	x = m.dense1.Forward(x, training)
	x = m.dense2.Forward(x, training)
	
	return x
}

// Backward performs backpropagation
func (m *SimpleCNN) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	// Backward through dense layers (ReLU gradient handled internally by dense1)
	grad := m.dense2.Backward(gradOutput)
	grad = m.dense1.Backward(grad)
	grad = m.flatten.Backward(grad)
	
	// Backward through conv block 2
	grad = m.dropout2.Backward(grad)
	grad = m.pool2.Backward(grad)
	grad = reluBackward(grad)
	grad = m.conv2.Backward(grad)
	
	// Backward through conv block 1
	grad = m.dropout1.Backward(grad)
	grad = m.pool1.Backward(grad)
	grad = reluBackward(grad)
	grad = m.conv1.Backward(grad)
	
	return grad
}

// UpdateWeights updates all layer weights
func (m *SimpleCNN) UpdateWeights(lr float64) {
	m.conv1.UpdateWeights(lr)
	m.conv2.UpdateWeights(lr)
	m.dense1.UpdateWeights(lr)
	m.dense2.UpdateWeights(lr)
}

// relu applies ReLU activation
func relu(x *utils.Tensor) *utils.Tensor {
	output := utils.NewTensor(x.Shape...)
	for i := range x.Data {
		if x.Data[i] > 0 {
			output.Data[i] = x.Data[i]
		}
	}
	return output
}

// reluBackward computes ReLU gradient
func reluBackward(grad *utils.Tensor) *utils.Tensor {
	output := utils.NewTensor(grad.Shape...)
	for i := range grad.Data {
		if grad.Data[i] > 0 {
			output.Data[i] = grad.Data[i]
		}
	}
	return output
}

// softmax applies softmax activation
func softmax(x *utils.Tensor) *utils.Tensor {
	output := utils.NewTensor(x.Shape...)
	
	// Process each sample in the batch
	batchSize := x.Shape[0]
	classCount := x.Shape[1]
	
	for b := 0; b < batchSize; b++ {
		// Find max for numerical stability
		maxVal := x.Data[b*classCount]
		for c := 1; c < classCount; c++ {
			if x.Data[b*classCount+c] > maxVal {
				maxVal = x.Data[b*classCount+c]
			}
		}
		
		// Compute exp and sum
		sum := 0.0
		for c := 0; c < classCount; c++ {
			output.Data[b*classCount+c] = math.Exp(x.Data[b*classCount+c] - maxVal)
			sum += output.Data[b*classCount+c]
		}
		
		// Normalize
		for c := 0; c < classCount; c++ {
			output.Data[b*classCount+c] /= sum
		}
	}
	
	return output
}

// crossEntropyLoss computes cross-entropy loss and gradient
// Note: Assumes predictions are already probabilities (after softmax)
func crossEntropyLoss(predictions *utils.Tensor, labels []int) (float64, *utils.Tensor) {
	batchSize := predictions.Shape[0]
	classCount := predictions.Shape[1]
	
	// Predictions should already be probabilities from softmax layer
	probs := predictions
	
	// Compute loss
	loss := 0.0
	for b := 0; b < batchSize; b++ {
		label := labels[b]
		prob := probs.Data[b*classCount+label]
		if prob > 0 {
			loss -= math.Log(prob)
		}
	}
	loss /= float64(batchSize)
	
	// Compute gradient for softmax + cross-entropy
	grad := utils.NewTensor(predictions.Shape...)
	for b := 0; b < batchSize; b++ {
		for c := 0; c < classCount; c++ {
			grad.Data[b*classCount+c] = probs.Data[b*classCount+c]
			if c == labels[b] {
				grad.Data[b*classCount+c] -= 1.0
			}
			grad.Data[b*classCount+c] /= float64(batchSize)
		}
	}
	
	return loss, grad
}

// accuracy computes classification accuracy
func accuracy(predictions *utils.Tensor, labels []int) float64 {
	correct := 0
	batchSize := predictions.Shape[0]
	classCount := predictions.Shape[1]
	
	for b := 0; b < batchSize; b++ {
		// Find predicted class
		maxIdx := 0
		maxVal := predictions.Data[b*classCount]
		for c := 1; c < classCount; c++ {
			if predictions.Data[b*classCount+c] > maxVal {
				maxVal = predictions.Data[b*classCount+c]
				maxIdx = c
			}
		}
		
		if maxIdx == labels[b] {
			correct++
		}
	}
	
	return float64(correct) / float64(batchSize)
}

// Train trains the model
func Train(model *SimpleCNN, xTrain, yTrain *utils.Tensor, epochs int, batchSize int, lr float64) {
	numSamples := xTrain.Shape[0]
	numBatches := numSamples / batchSize
	
	fmt.Printf("Training on %d samples, %d batches per epoch\n", numSamples, numBatches)
	
	for epoch := 1; epoch <= epochs; epoch++ {
		epochLoss := 0.0
		epochAcc := 0.0
		
		// Shuffle training data
		perm := rand.Perm(numSamples)
		
		for batch := 0; batch < numBatches; batch++ {
			// Prepare batch
			batchImages := utils.NewTensor(batchSize, 1, 28, 28)
			batchLabels := make([]int, batchSize)
			
			for i := 0; i < batchSize; i++ {
				idx := perm[batch*batchSize+i]
				// Copy flattened image and reshape to (1, 28, 28)
				for j := 0; j < 784; j++ {
					batchImages.Data[i*784+j] = xTrain.Data[idx*784+j]
				}
				// Get label from one-hot encoding
				maxIdx := 0
				maxVal := yTrain.Data[idx*10]
				for c := 1; c < 10; c++ {
					if yTrain.Data[idx*10+c] > maxVal {
						maxVal = yTrain.Data[idx*10+c]
						maxIdx = c
					}
				}
				batchLabels[i] = maxIdx
			}
			
			// Forward pass
			predictions := model.Forward(batchImages, true)
			
			// Compute loss and gradient
			loss, grad := crossEntropyLoss(predictions, batchLabels)
			epochLoss += loss
			
			// Compute accuracy
			acc := accuracy(predictions, batchLabels)
			epochAcc += acc
			
			// Backward pass
			model.Backward(grad)
			
			// Update weights
			model.UpdateWeights(lr)
			
			// Print progress
			if batch%100 == 0 {
				fmt.Printf("Epoch %d, Batch %d/%d, Loss: %.4f, Accuracy: %.2f%%\n", 
					epoch, batch, numBatches, loss, acc*100)
			}
		}
		
		avgLoss := epochLoss / float64(numBatches)
		avgAcc := epochAcc / float64(numBatches)
		fmt.Printf("Epoch %d Complete - Avg Loss: %.4f, Avg Accuracy: %.2f%%\n", 
			epoch, avgLoss, avgAcc*100)
	}
}

// Evaluate evaluates the model on test data
func Evaluate(model *SimpleCNN, xTest, yTest *utils.Tensor) {
	numSamples := xTest.Shape[0]
	batchSize := 100
	numBatches := numSamples / batchSize
	
	totalAcc := 0.0
	totalLoss := 0.0
	
	for batch := 0; batch < numBatches; batch++ {
		// Prepare batch
		batchImages := utils.NewTensor(batchSize, 1, 28, 28)
		batchLabels := make([]int, batchSize)
		
		for i := 0; i < batchSize; i++ {
			idx := batch*batchSize + i
			// Copy flattened image and reshape to (1, 28, 28)
			for j := 0; j < 784; j++ {
				batchImages.Data[i*784+j] = xTest.Data[idx*784+j]
			}
			// Get label from one-hot encoding
			maxIdx := 0
			maxVal := yTest.Data[idx*10]
			for c := 1; c < 10; c++ {
				if yTest.Data[idx*10+c] > maxVal {
					maxVal = yTest.Data[idx*10+c]
					maxIdx = c
				}
			}
			batchLabels[i] = maxIdx
		}
		
		// Forward pass (inference mode)
		predictions := model.Forward(batchImages, false)
		
		// Compute metrics
		loss, _ := crossEntropyLoss(predictions, batchLabels)
		acc := accuracy(predictions, batchLabels)
		
		totalLoss += loss
		totalAcc += acc
	}
	
	avgLoss := totalLoss / float64(numBatches)
	avgAcc := totalAcc / float64(numBatches)
	
	fmt.Printf("\nTest Results:\n")
	fmt.Printf("Average Loss: %.4f\n", avgLoss)
	fmt.Printf("Average Accuracy: %.2f%%\n", avgAcc*100)
}

// Predict performs inference on a single image
func Predict(model *SimpleCNN, image []float64) int {
	// Prepare input tensor
	input := utils.NewTensor(1, 1, 28, 28)
	copy(input.Data, image)
	
	// Forward pass
	output := model.Forward(input, false)
	
	// Find predicted class
	maxIdx := 0
	maxVal := output.Data[0]
	for i := 1; i < 10; i++ {
		if output.Data[i] > maxVal {
			maxVal = output.Data[i]
			maxIdx = i
		}
	}
	
	return maxIdx
}

// VisualizeImage shows an ASCII representation of an MNIST image
func VisualizeImage(image []float64, label int) {
	fmt.Printf("\nLabel: %d\n", label)
	for i := 0; i < 28; i++ {
		for j := 0; j < 28; j++ {
			pixel := image[i*28+j]
			if pixel > 0.7 {
				fmt.Print("█")
			} else if pixel > 0.3 {
				fmt.Print("▒")
			} else if pixel > 0.1 {
				fmt.Print("░")
			} else {
				fmt.Print(" ")
			}
		}
		fmt.Println()
	}
}

func main() {
	// Set random seed for reproducibility
	rand.Seed(time.Now().UnixNano())
	
	fmt.Println("=== MNIST CNN Training Example ===")
	fmt.Println("Using embedded MNIST dataset from go/neuro/data")
	fmt.Println()
	
	// Load MNIST data using the vendored loader
	// For quick demo, just load a subset
	fmt.Println("Loading MNIST data subset for demonstration...")
	xTrain, yTrain, xTest, yTest, err := data.LoadVendoredMNISTSubset(1000, 200)
	if err != nil {
		fmt.Printf("Error loading MNIST data: %v\n", err)
		fmt.Println("\nRunning demo mode instead...")
		runDemo()
		return
	}
	
	fmt.Printf("Loaded %d training samples and %d test samples\n", 
		xTrain.Shape[0], xTest.Shape[0])
	
	// Create model
	fmt.Println("\nCreating CNN model...")
	model := NewSimpleCNN()
	
	// Training parameters
	epochs := 1  // Reduced for quick demonstration
	batchSize := 32
	learningRate := 0.001
	
	fmt.Printf("\nTraining parameters:\n")
	fmt.Printf("- Epochs: %d\n", epochs)
	fmt.Printf("- Batch Size: %d\n", batchSize)
	fmt.Printf("- Learning Rate: %.4f\n", learningRate)
	fmt.Println()
	
	// Train the model
	fmt.Println("Starting training...")
	Train(model, xTrain, yTrain, epochs, batchSize, learningRate)
	
	// Evaluate on test set
	fmt.Println("\nEvaluating on test set...")
	Evaluate(model, xTest, yTest)
	
	// Demonstrate inference on a few samples
	fmt.Println("\n=== Inference Examples ===")
	for i := 0; i < 5; i++ {
		idx := rand.Intn(xTest.Shape[0])
		
		// Extract single image
		image := make([]float64, 784)
		for j := 0; j < 784; j++ {
			image[j] = xTest.Data[idx*784+j]
		}
		
		// Get actual label from one-hot
		actual := 0
		maxVal := yTest.Data[idx*10]
		for c := 1; c < 10; c++ {
			if yTest.Data[idx*10+c] > maxVal {
				maxVal = yTest.Data[idx*10+c]
				actual = c
			}
		}
		
		prediction := Predict(model, image)
		
		fmt.Printf("\nSample %d:\n", i+1)
		VisualizeImage(image, actual)
		fmt.Printf("Predicted: %d, Actual: %d", prediction, actual)
		if prediction == actual {
			fmt.Println(" ✓")
		} else {
			fmt.Println(" ✗")
		}
	}
}

// runDemo runs a demonstration with synthetic data
func runDemo() {
	fmt.Println("\n=== CNN Architecture Demo ===")
	
	// Create model
	model := NewSimpleCNN()
	
	// Create synthetic batch
	batchSize := 4
	input := utils.NewTensor(batchSize, 1, 28, 28)
	
	// Fill with random data
	for i := range input.Data {
		input.Data[i] = rand.Float64()
	}
	
	// Create random labels
	labels := make([]int, batchSize)
	for i := range labels {
		labels[i] = rand.Intn(10)
	}
	
	fmt.Println("\nModel Architecture:")
	fmt.Println("Input: (batch_size, 1, 28, 28)")
	fmt.Println("├─ Conv2D(1, 32, 3x3) -> (batch_size, 32, 26, 26)")
	fmt.Println("├─ ReLU")
	fmt.Println("├─ MaxPool2D(2x2, stride=2) -> (batch_size, 32, 13, 13)")
	fmt.Println("├─ Dropout(0.25)")
	fmt.Println("├─ Conv2D(32, 64, 3x3) -> (batch_size, 64, 11, 11)")
	fmt.Println("├─ ReLU")
	fmt.Println("├─ MaxPool2D(2x2, stride=2) -> (batch_size, 64, 5, 5)")
	fmt.Println("├─ Dropout(0.5)")
	fmt.Println("├─ Flatten -> (batch_size, 1600)")
	fmt.Println("├─ Dense(1600, 128)")
	fmt.Println("├─ ReLU")
	fmt.Println("├─ Dense(128, 10)")
	fmt.Println("└─ Softmax -> (batch_size, 10)")
	
	fmt.Printf("\nRunning forward pass with batch size %d...\n", batchSize)
	
	// Forward pass
	startTime := time.Now()
	output := model.Forward(input, true)
	forwardTime := time.Since(startTime)
	
	fmt.Printf("Forward pass completed in %v\n", forwardTime)
	fmt.Printf("Output shape: %v\n", output.Shape)
	
	// Compute loss
	loss, grad := crossEntropyLoss(output, labels)
	fmt.Printf("Cross-entropy loss: %.4f\n", loss)
	
	// Backward pass
	startTime = time.Now()
	model.Backward(grad)
	backwardTime := time.Since(startTime)
	
	fmt.Printf("Backward pass completed in %v\n", backwardTime)
	
	// Update weights
	model.UpdateWeights(0.001)
	fmt.Println("Weights updated")
	
	// Show some statistics
	fmt.Println("\n=== Layer Statistics ===")
	
	// Conv1 weights
	conv1Params := model.conv1.GetParams()
	if len(conv1Params) > 0 {
		weights := conv1Params[0]
		var mean, std float64
		for _, w := range weights.Data {
			mean += w
		}
		mean /= float64(len(weights.Data))
		for _, w := range weights.Data {
			std += (w - mean) * (w - mean)
		}
		std = math.Sqrt(std / float64(len(weights.Data)))
		fmt.Printf("Conv1 weights - Mean: %.6f, Std: %.6f\n", mean, std)
	}
	
	// Conv2 weights
	conv2Params := model.conv2.GetParams()
	if len(conv2Params) > 0 {
		weights := conv2Params[0]
		var mean, std float64
		for _, w := range weights.Data {
			mean += w
		}
		mean /= float64(len(weights.Data))
		for _, w := range weights.Data {
			std += (w - mean) * (w - mean)
		}
		std = math.Sqrt(std / float64(len(weights.Data)))
		fmt.Printf("Conv2 weights - Mean: %.6f, Std: %.6f\n", mean, std)
	}
	
	fmt.Println("\n=== Demo Complete ===")
	fmt.Println("To train on real MNIST data, download the dataset from:")
	fmt.Println("http://yann.lecun.com/exdb/mnist/")
	fmt.Println("\nRequired files:")
	fmt.Println("- train-images-idx3-ubyte")
	fmt.Println("- train-labels-idx1-ubyte")  
	fmt.Println("- t10k-images-idx3-ubyte")
	fmt.Println("- t10k-labels-idx1-ubyte")
}