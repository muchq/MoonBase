package main

import (
	"flag"
	"fmt"
	"math"
	"math/rand"
	"os"
	"time"

	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/data"
	"github.com/muchq/moonbase/go/neuro/inference"
	"github.com/muchq/moonbase/go/neuro/layers"
	"github.com/muchq/moonbase/go/neuro/network"
	"github.com/muchq/moonbase/go/neuro/utils"
)

// CNNModel wraps our CNN architecture for use with the network.Model interface
type CNNModel struct {
	model *network.Model
	// Layer references for direct access during forward/backward
	conv1    *layers.Conv2D
	pool1    *layers.MaxPool2D
	conv2    *layers.Conv2D
	pool2    *layers.MaxPool2D
	flatten  *layers.Flatten
	dense1   *layers.Dense
	dense2   *layers.Dense
	dropout1 *layers.Dropout
	dropout2 *layers.Dropout
	// Cache for ReLU backward pass
	conv1Output *utils.Tensor
	conv2Output *utils.Tensor
}

// NewCNNModel creates a new CNN model for MNIST
func NewCNNModel() *CNNModel {
	model := network.NewModel()

	// Create layers
	conv1 := layers.NewConv2D(1, 32, []int{3, 3}, 1, "valid", true)
	pool1 := layers.NewMaxPool2D([]int{2, 2}, 2, "valid")
	dropout1 := layers.NewDropout(0.25)

	conv2 := layers.NewConv2D(32, 64, []int{3, 3}, 1, "valid", true)
	pool2 := layers.NewMaxPool2D([]int{2, 2}, 2, "valid")
	dropout2 := layers.NewDropout(0.5)

	flatten := layers.NewFlatten()
	dense1 := layers.NewDense(1600, 128, &activations.ReLU{}) // 5*5*64 = 1600
	dense2 := layers.NewDense(128, 10, &activations.Softmax{})

	// Add layers to model
	model.Add(conv1)
	model.Add(pool1)
	model.Add(dropout1)
	model.Add(conv2)
	model.Add(pool2)
	model.Add(dropout2)
	model.Add(flatten)
	model.Add(dense1)
	model.Add(dense2)

	return &CNNModel{
		model:    model,
		conv1:    conv1,
		pool1:    pool1,
		conv2:    conv2,
		pool2:    pool2,
		flatten:  flatten,
		dense1:   dense1,
		dense2:   dense2,
		dropout1: dropout1,
		dropout2: dropout2,
	}
}

// NewCNNModelFromLoaded creates a CNN model from a loaded network.Model
func NewCNNModelFromLoaded(loadedModel *network.Model) *CNNModel {
	modelLayers := loadedModel.GetLayers()
	
	// Extract the specific layer references
	// The order must match how they were added in NewCNNModel
	return &CNNModel{
		model:    loadedModel,
		conv1:    modelLayers[0].(*layers.Conv2D),
		pool1:    modelLayers[1].(*layers.MaxPool2D),
		dropout1: modelLayers[2].(*layers.Dropout),
		conv2:    modelLayers[3].(*layers.Conv2D),
		pool2:    modelLayers[4].(*layers.MaxPool2D),
		dropout2: modelLayers[5].(*layers.Dropout),
		flatten:  modelLayers[6].(*layers.Flatten),
		dense1:   modelLayers[7].(*layers.Dense),
		dense2:   modelLayers[8].(*layers.Dense),
	}
}

// Forward performs forward propagation using the structured approach
func (m *CNNModel) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	// Conv block 1
	x := m.conv1.Forward(input, training)
	// Cache conv output for backward pass
	if training {
		m.conv1Output = x.Copy()
	}
	x = relu(x)
	x = m.pool1.Forward(x, training)
	x = m.dropout1.Forward(x, training)

	// Conv block 2
	x = m.conv2.Forward(x, training)
	// Cache conv output for backward pass
	if training {
		m.conv2Output = x.Copy()
	}
	x = relu(x)
	x = m.pool2.Forward(x, training)
	x = m.dropout2.Forward(x, training)

	// Dense layers (already have ReLU and Softmax built-in)
	x = m.flatten.Forward(x, training)
	x = m.dense1.Forward(x, training)
	x = m.dense2.Forward(x, training)

	return x
}

// Backward performs backpropagation
func (m *CNNModel) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	// Backward through dense layers
	grad := m.dense2.Backward(gradOutput)
	grad = m.dense1.Backward(grad)
	grad = m.flatten.Backward(grad)

	// Backward through conv block 2
	grad = m.dropout2.Backward(grad)
	grad = m.pool2.Backward(grad)
	grad = reluBackward(grad, m.conv2Output)
	grad = m.conv2.Backward(grad)

	// Backward through conv block 1
	grad = m.dropout1.Backward(grad)
	grad = m.pool1.Backward(grad)
	grad = reluBackward(grad, m.conv1Output)
	grad = m.conv1.Backward(grad)

	return grad
}

// UpdateWeights updates all layer weights
func (m *CNNModel) UpdateWeights(lr float64) {
	m.conv1.UpdateWeights(lr)
	m.conv2.UpdateWeights(lr)
	m.dense1.UpdateWeights(lr)
	m.dense2.UpdateWeights(lr)
}

// GetModel returns the underlying network.Model for serialization
func (m *CNNModel) GetModel() *network.Model {
	return m.model
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
func reluBackward(grad *utils.Tensor, cache *utils.Tensor) *utils.Tensor {
	output := utils.NewTensor(grad.Shape...)
	for i := range grad.Data {
		// Only pass gradient where input was > 0
		if cache.Data[i] > 0 {
			output.Data[i] = grad.Data[i]
		}
	}
	return output
}

// crossEntropyLoss computes cross-entropy loss and gradient
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

// TrainWithResume trains the model with full checkpoint and resume support
func TrainWithResume(model *CNNModel, xTrain, yTrain *utils.Tensor, startEpoch, startBatch, totalEpochs int, 
	batchSize int, lr float64, checkpointManager *inference.CheckpointManager, resumePerm []int, randomSeed int64) {
	
	numSamples := xTrain.Shape[0]
	numBatches := numSamples / batchSize

	fmt.Printf("Training on %d samples, %d batches per epoch\n", numSamples, numBatches)
	
	// Set random seed for reproducibility
	if randomSeed > 0 {
		rand.Seed(randomSeed)
	}

	for epoch := startEpoch; epoch <= totalEpochs; epoch++ {
		epochStart := time.Now()
		epochLoss := 0.0
		epochAcc := 0.0
		
		// Determine starting batch for this epoch
		startBatchForEpoch := 0
		if epoch == startEpoch && startBatch > 0 {
			startBatchForEpoch = startBatch
			fmt.Printf("Resuming from batch %d in epoch %d\n", startBatch, epoch)
		}

		// Use resumed permutation or create new one
		var perm []int
		if epoch == startEpoch && len(resumePerm) > 0 {
			perm = resumePerm
			fmt.Printf("Using resumed data permutation\n")
		} else {
			// Generate new permutation with current seed
			perm = rand.Perm(numSamples)
		}
		
		// Save checkpoint at the beginning of each epoch (with permutation)
		if startBatchForEpoch == 0 && checkpointManager != nil {
			// This is the start of a new epoch, save the permutation
			currentSeed := rand.Int63()
			if err := checkpointManager.SaveCheckpointWithProgress(model.GetModel(), epoch, 0, 0.0, perm, currentSeed); err != nil {
				fmt.Printf("Warning: Failed to save epoch start checkpoint: %v\n", err)
			}
		}

		// Train for this epoch
		for batch := startBatchForEpoch; batch < numBatches; batch++ {
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

			// Print progress every 10 batches
			if batch%10 == 0 && batch > 0 {
				fmt.Printf("Epoch %d, Batch %d/%d, Loss: %.4f, Accuracy: %.2f%%\n",
					epoch, batch, numBatches, loss, acc*100)
			}
			
			// Save mid-epoch checkpoint periodically (every 100 batches)
			if checkpointManager != nil && batch > 0 && batch % 100 == 0 {
				currentSeed := rand.Int63()
				avgBatchLoss := epochLoss / float64(batch - startBatchForEpoch + 1)
				if err := checkpointManager.SaveCheckpointWithProgress(model.GetModel(), epoch, batch, avgBatchLoss, perm, currentSeed); err != nil {
					fmt.Printf("Warning: Failed to save mid-epoch checkpoint: %v\n", err)
				} else {
					fmt.Printf("Saved checkpoint at epoch %d, batch %d\n", epoch, batch)
				}
			}
		}

		// Calculate averages - account for resumed training
		var avgLoss, avgAcc float64
		if startBatchForEpoch > 0 {
			// We resumed mid-epoch, calculate based on batches completed
			batchesCompleted := numBatches - startBatchForEpoch
			avgLoss = epochLoss / float64(batchesCompleted)
			avgAcc = epochAcc / float64(batchesCompleted)
		} else {
			avgLoss = epochLoss / float64(numBatches)
			avgAcc = epochAcc / float64(numBatches)
		}
		epochTime := time.Since(epochStart)

		fmt.Printf("Epoch %d Complete - Avg Loss: %.4f, Avg Accuracy: %.2f%%, Time: %v\n",
			epoch, avgLoss, avgAcc*100, epochTime)

		// Save end-of-epoch checkpoint with fresh permutation for next epoch
		if checkpointManager != nil {
			fmt.Printf("Saving end-of-epoch checkpoint for epoch %d...\n", epoch)
			currentSeed := rand.Int63()
			// Save with batch=-1 to indicate end of epoch
			if err := checkpointManager.SaveCheckpointWithProgress(model.GetModel(), epoch, -1, avgLoss, nil, currentSeed); err != nil {
				fmt.Printf("Warning: Failed to save checkpoint: %v\n", err)
			}
		}

		// Decay learning rate every 5 epochs
		if epoch%5 == 0 && epoch > 0 {
			lr *= 0.9
			fmt.Printf("Learning rate decayed to: %.6f\n", lr)
		}
	}
}

// Evaluate evaluates the model on test data
func Evaluate(model *CNNModel, xTest, yTest *utils.Tensor) {
	numSamples := xTest.Shape[0]
	batchSize := 100
	numBatches := numSamples / batchSize

	totalAcc := 0.0
	totalLoss := 0.0

	fmt.Printf("Evaluating on %d test samples...\n", numSamples)

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

		// Print progress every 20 batches
		if batch%20 == 0 && batch > 0 {
			fmt.Printf("Evaluated %d/%d batches\n", batch, numBatches)
		}
	}

	avgLoss := totalLoss / float64(numBatches)
	avgAcc := totalAcc / float64(numBatches)

	fmt.Printf("\nTest Results:\n")
	fmt.Printf("Average Loss: %.4f\n", avgLoss)
	fmt.Printf("Average Accuracy: %.2f%%\n", avgAcc*100)
}

// Predict performs inference on a single image
func Predict(model *CNNModel, image []float64) int {
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

// isFlagPassed checks if a flag was explicitly set
func isFlagPassed(name string) bool {
	found := false
	flag.Visit(func(f *flag.Flag) {
		if f.Name == name {
			found = true
		}
	})
	return found
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
	// Parse command-line flags
	trainSize := flag.Int("train", 0, "Number of training samples (0 for full dataset)")
	testSize := flag.Int("test", 0, "Number of test samples (0 for full dataset)")
	epochs := flag.Int("epochs", 10, "Number of training epochs")
	batchSize := flag.Int("batch", 64, "Batch size")
	learningRate := flag.Float64("lr", 0.001, "Learning rate")
	flag.Parse()
	
	// Set random seed for reproducibility
	rand.Seed(42)

	fmt.Println("=== MNIST CNN Full Training with Model Serialization ===")
	fmt.Println("Using embedded MNIST dataset from go/neuro/data")
	fmt.Println()

	// Load MNIST dataset
	var xTrain, yTrain, xTest, yTest *utils.Tensor
	var err error
	startTime := time.Now()
	
	if *trainSize > 0 || *testSize > 0 {
		fmt.Printf("Loading MNIST subset: %d training, %d test samples...\n", *trainSize, *testSize)
		xTrain, yTrain, xTest, yTest, err = data.LoadVendoredMNISTSubset(*trainSize, *testSize)
	} else {
		fmt.Println("Loading full MNIST dataset...")
		xTrain, yTrain, xTest, yTest, err = data.LoadVendoredMNIST()
	}
	
	if err != nil {
		fmt.Printf("Error loading MNIST data: %v\n", err)
		return
	}
	loadTime := time.Since(startTime)

	fmt.Printf("Loaded %d training samples and %d test samples in %v\n",
		xTrain.Shape[0], xTest.Shape[0], loadTime)
	
	fmt.Println("\nUsage: bazel run //go/neuro/examples:mnist_cnn_full -- -train=5000 -test=1000 -epochs=20 -batch=64 -lr=0.001")
	fmt.Println("Omit flags to use defaults (full dataset, 10 epochs, batch 64, lr 0.001)")

	// Create model
	fmt.Println("\nCreating CNN model...")
	model := NewCNNModel()

	// Set up checkpoint manager
	checkpointDir := "models/mnist_cnn_checkpoints"
	fmt.Printf("Checkpoints will be saved to: %s\n", checkpointDir)
	checkpointManager := inference.NewCheckpointManager(checkpointDir, 3) // Keep last 3 checkpoints

	// Adjust epochs for smaller datasets if not explicitly set
	if *trainSize > 0 && *trainSize <= 5000 && !isFlagPassed("epochs") {
		*epochs = 20  // More epochs for smaller datasets
	}

	// Check if we should resume from checkpoint
	startEpoch := 1
	startBatch := 0
	actualLearningRate := *learningRate
	var resumePerm []int
	var randomSeed int64
	
	if _, err := os.Stat(checkpointDir); err == nil {
		fmt.Println("\nFound existing checkpoints. Loading latest checkpoint...")
		if state, err := checkpointManager.LoadLatestCheckpointWithFullState(); err == nil {
			// Replace the model with the loaded one
			model = NewCNNModelFromLoaded(state.Model)
			
			// Determine where to resume from
			if state.Batch >= 0 {
				// Mid-epoch checkpoint - resume from next batch
				startEpoch = state.Epoch
				startBatch = state.Batch + 1
				resumePerm = state.Permutation
				randomSeed = state.RandomSeed
				fmt.Printf("Successfully resumed from checkpoint (epoch %d, batch %d)!\n", state.Epoch, state.Batch)
			} else {
				// End-of-epoch checkpoint - start next epoch
				startEpoch = state.Epoch + 1
				startBatch = 0
				randomSeed = state.RandomSeed
				fmt.Printf("Successfully resumed from checkpoint (end of epoch %d)!\n", state.Epoch)
			}
			
			// Calculate the actual learning rate after decay
			// Learning rate decays by 0.9 every 5 epochs
			completedEpochs := state.Epoch
			if state.Batch < 0 {
				// If we completed the epoch, count it
				completedEpochs = state.Epoch
			} else {
				// If mid-epoch, don't count current epoch as completed
				completedEpochs = state.Epoch - 1
			}
			
			numDecays := completedEpochs / 5
			for i := 0; i < numDecays; i++ {
				actualLearningRate *= 0.9
			}
			
			fmt.Printf("Adjusted learning rate to: %.6f (after %d decay steps)\n", actualLearningRate, numDecays)
		} else {
			fmt.Printf("Could not load checkpoint: %v\n", err)
			fmt.Println("Starting fresh training...")
			// Initialize random seed for new training
			randomSeed = time.Now().UnixNano()
		}
	} else {
		// No checkpoints, start fresh
		randomSeed = time.Now().UnixNano()
	}

	fmt.Printf("\nTraining parameters:\n")
	fmt.Printf("- Epochs: %d\n", *epochs)
	fmt.Printf("- Batch Size: %d\n", *batchSize)
	if startEpoch == 1 {
		fmt.Printf("- Initial Learning Rate: %.4f\n", *learningRate)
	} else {
		fmt.Printf("- Current Learning Rate: %.6f (resumed from epoch %d)\n", actualLearningRate, startEpoch-1)
	}
	fmt.Printf("- Learning Rate Decay: 0.9 every 5 epochs\n")
	fmt.Println()

	// Train the model
	if startEpoch <= *epochs || startBatch > 0 {
		if startBatch > 0 {
			fmt.Printf("Resuming training from epoch %d, batch %d...\n", startEpoch, startBatch)
		} else {
			fmt.Printf("Starting training from epoch %d...\n", startEpoch)
		}
		trainStart := time.Now()
		TrainWithResume(model, xTrain, yTrain, startEpoch, startBatch, *epochs, *batchSize, actualLearningRate, checkpointManager, resumePerm, randomSeed)
		trainTime := time.Since(trainStart)
		fmt.Printf("\nTotal training time: %v\n", trainTime)
	} else {
		fmt.Printf("Model already trained for %d epochs (target was %d epochs)\n", startEpoch-1, *epochs)
	}

	// Evaluate on test set
	fmt.Println("\nEvaluating on test set...")
	evalStart := time.Now()
	Evaluate(model, xTest, yTest)
	evalTime := time.Since(evalStart)
	fmt.Printf("Evaluation time: %v\n", evalTime)

	// Save final model for production use
	finalModelPath := "models/mnist_cnn_final"
	fmt.Printf("\nSaving final model to %s...\n", finalModelPath)
	if err := inference.SaveModel(model.GetModel(), finalModelPath); err != nil {
		fmt.Printf("Error saving model: %v\n", err)
	} else {
		fmt.Println("Model saved successfully!")
		fmt.Println("\nTo use this model in production, run:")
		fmt.Println("  bazel run //go/neuro/examples:mnist_cnn_production")
	}

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

		// Time single inference
		inferStart := time.Now()
		prediction := Predict(model, image)
		inferTime := time.Since(inferStart)

		fmt.Printf("\nSample %d (Inference time: %v):\n", i+1, inferTime)
		VisualizeImage(image, actual)
		fmt.Printf("Predicted: %d, Actual: %d", prediction, actual)
		if prediction == actual {
			fmt.Println(" ✓")
		} else {
			fmt.Println(" ✗")
		}
	}

	// Print model statistics
	fmt.Println("\n=== Model Statistics ===")
	params := model.GetModel().GetParams()
	totalParams := 0
	for _, p := range params {
		totalParams += len(p)
	}
	fmt.Printf("Total trainable parameters: %d\n", totalParams)
	fmt.Printf("Model architecture:\n")
	fmt.Println("  Conv2D(1, 32, 3x3) -> 320 params")
	fmt.Println("  MaxPool2D(2x2)")
	fmt.Println("  Dropout(0.25)")
	fmt.Println("  Conv2D(32, 64, 3x3) -> 18,496 params")
	fmt.Println("  MaxPool2D(2x2)")
	fmt.Println("  Dropout(0.5)")
	fmt.Println("  Flatten()")
	fmt.Println("  Dense(1600, 128) -> 204,928 params")
	fmt.Println("  Dense(128, 10) -> 1,290 params")
	fmt.Printf("  Total: %d parameters\n", totalParams)
}
