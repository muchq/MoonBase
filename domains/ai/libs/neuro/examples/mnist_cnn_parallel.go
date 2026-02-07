package main

import (
	"fmt"
	"math"
	"math/rand"
	"runtime"
	"time"

	"github.com/muchq/moonbase/domains/ai/libs/neuro/activations"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/data"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/inference"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/layers"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/network"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
)

// ParallelCNNModel wraps CNN with parallel training support
type ParallelCNNModel struct {
	models   []*CNNModel  // Multiple model replicas for parallel training
	master   *CNNModel    // Master model that aggregates updates
	numWorkers int
}

// CNNModel represents a single CNN instance
type CNNModel struct {
	model    *network.Model
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
	reluCache1 *utils.Tensor
	reluCache2 *utils.Tensor
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
	dense1 := layers.NewDense(1600, 128, &activations.ReLU{})
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

// NewParallelCNNModel creates a parallel CNN trainer
func NewParallelCNNModel(numWorkers int) *ParallelCNNModel {
	master := NewCNNModel()
	models := make([]*CNNModel, numWorkers)
	
	// Create worker models
	for i := 0; i < numWorkers; i++ {
		models[i] = NewCNNModel()
		// Initialize with master weights
		models[i].CopyWeightsFrom(master)
	}
	
	return &ParallelCNNModel{
		models:     models,
		master:     master,
		numWorkers: numWorkers,
	}
}

// CopyWeightsFrom copies weights from another model
func (m *CNNModel) CopyWeightsFrom(source *CNNModel) {
	// Copy conv1 weights
	srcParams := source.conv1.GetParams()
	m.conv1.SetParams(srcParams)
	
	// Copy conv2 weights
	srcParams = source.conv2.GetParams()
	m.conv2.SetParams(srcParams)
	
	// Copy dense1 weights
	srcParams = source.dense1.GetParams()
	m.dense1.SetParams(srcParams)
	
	// Copy dense2 weights
	srcParams = source.dense2.GetParams()
	m.dense2.SetParams(srcParams)
}

// Forward performs forward propagation
func (m *CNNModel) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	// Conv block 1
	x := m.conv1.Forward(input, training)
	m.reluCache1 = x.Copy() // Cache for backward pass
	x = relu(x)
	x = m.pool1.Forward(x, training)
	x = m.dropout1.Forward(x, training)
	
	// Conv block 2
	x = m.conv2.Forward(x, training)
	m.reluCache2 = x.Copy() // Cache for backward pass
	x = relu(x)
	x = m.pool2.Forward(x, training)
	x = m.dropout2.Forward(x, training)
	
	// Dense layers
	x = m.flatten.Forward(x, training)
	x = m.dense1.Forward(x, training)
	x = m.dense2.Forward(x, training)
	
	return x
}

// Backward performs backpropagation
func (m *CNNModel) Backward(gradOutput *utils.Tensor) *utils.Tensor {
	grad := m.dense2.Backward(gradOutput)
	grad = m.dense1.Backward(grad)
	grad = m.flatten.Backward(grad)
	
	grad = m.dropout2.Backward(grad)
	grad = m.pool2.Backward(grad)
	grad = reluBackward(grad, m.reluCache2)
	grad = m.conv2.Backward(grad)
	
	grad = m.dropout1.Backward(grad)
	grad = m.pool1.Backward(grad)
	grad = reluBackward(grad, m.reluCache1)
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

// ClearGradients clears all accumulated gradients
func (m *CNNModel) ClearGradients() {
	// Clear Conv2D gradients
	conv1Grads := m.conv1.GetGradients()
	for _, grad := range conv1Grads {
		if grad != nil {
			for i := range grad.Data {
				grad.Data[i] = 0
			}
		}
	}
	
	conv2Grads := m.conv2.GetGradients()
	for _, grad := range conv2Grads {
		if grad != nil {
			for i := range grad.Data {
				grad.Data[i] = 0
			}
		}
	}
	
	// Clear Dense layer gradients
	if m.dense1.GradW != nil {
		for i := range m.dense1.GradW.Data {
			m.dense1.GradW.Data[i] = 0
		}
	}
	if m.dense1.GradB != nil {
		for i := range m.dense1.GradB.Data {
			m.dense1.GradB.Data[i] = 0
		}
	}
	if m.dense2.GradW != nil {
		for i := range m.dense2.GradW.Data {
			m.dense2.GradW.Data[i] = 0
		}
	}
	if m.dense2.GradB != nil {
		for i := range m.dense2.GradB.Data {
			m.dense2.GradB.Data[i] = 0
		}
	}
}

// GetGradients collects all gradients from the model
func (m *CNNModel) GetGradients() [][]*utils.Tensor {
	grads := make([][]*utils.Tensor, 4)
	
	grads[0] = m.conv1.GetGradients()  // conv1 gradients
	grads[1] = m.conv2.GetGradients()  // conv2 gradients
	grads[2] = m.dense1.GetGradients() // dense1 gradients
	grads[3] = m.dense2.GetGradients() // dense2 gradients
	
	return grads
}

// BatchWorker processes a batch in parallel
type BatchWorker struct {
	id          int
	model       *CNNModel
	batchImages *utils.Tensor
	batchLabels []int
	loss        float64
	accuracy    float64
	numSamples  int  // Track number of samples processed by this worker
	done        chan bool
}

// ProcessBatch processes a single batch
func (w *BatchWorker) ProcessBatch() {
	// Forward pass
	predictions := w.model.Forward(w.batchImages, true)
	
	// Compute loss and gradient
	loss, grad := crossEntropyLoss(predictions, w.batchLabels)
	w.loss = loss * float64(w.numSamples)  // Weight loss by number of samples
	
	// Compute accuracy
	correct := 0
	batchSize := predictions.Shape[0]
	classCount := predictions.Shape[1]
	
	for b := 0; b < batchSize; b++ {
		maxIdx := 0
		maxVal := predictions.Data[b*classCount]
		for c := 1; c < classCount; c++ {
			if predictions.Data[b*classCount+c] > maxVal {
				maxVal = predictions.Data[b*classCount+c]
				maxIdx = c
			}
		}
		
		if maxIdx == w.batchLabels[b] {
			correct++
		}
	}
	
	w.accuracy = float64(correct)  // Store raw correct count, not percentage
	
	// Backward pass
	w.model.Backward(grad)
	
	w.done <- true
}

// TrainParallel trains the model using parallel batch processing
func TrainParallel(pm *ParallelCNNModel, xTrain, yTrain *utils.Tensor, epochs int, batchSize int, lr float64, checkpointManager *inference.CheckpointManager, xTest, yTest *utils.Tensor) {
	numSamples := xTrain.Shape[0]
	numBatches := numSamples / batchSize
	
	fmt.Printf("Training on %d samples, %d batches per epoch\n", numSamples, numBatches)
	fmt.Printf("Using %d parallel workers, splitting each batch among workers\n", pm.numWorkers)
	
	for epoch := 1; epoch <= epochs; epoch++ {
		epochStart := time.Now()
		epochLoss := 0.0
		epochAcc := 0.0
		
		// Shuffle training data
		perm := rand.Perm(numSamples)
		
		// Process batches
		for batch := 0; batch < numBatches; batch++ {
			// Split single batch among workers
			samplesPerWorker := batchSize / pm.numWorkers
			remainingSamples := batchSize % pm.numWorkers
			
			workers := make([]*BatchWorker, 0, pm.numWorkers)
			
			for w := 0; w < pm.numWorkers; w++ {
				// Calculate samples for this worker
				workerSamples := samplesPerWorker
				if w < remainingSamples {
					workerSamples++
				}
				
				if workerSamples == 0 {
					continue
				}
				
				// Calculate start index for this worker's portion
				startIdx := w * samplesPerWorker + minInt(w, remainingSamples)
				
				// Prepare worker's portion of batch data
				workerImages := utils.NewTensor(workerSamples, 1, 28, 28)
				workerLabels := make([]int, workerSamples)
				
				for j := 0; j < workerSamples; j++ {
					dataIdx := perm[batch*batchSize + startIdx + j]
					if dataIdx >= numSamples {
						break
					}
					// Copy flattened image and reshape to (1, 28, 28)
					for k := 0; k < 784; k++ {
						workerImages.Data[j*784+k] = xTrain.Data[dataIdx*784+k]
					}
					// Get label from one-hot encoding
					workerLabels[j] = getLabel(yTrain, dataIdx)
				}
				
				// Create worker
				worker := &BatchWorker{
					id:          w,
					model:       pm.models[w],
					batchImages: workerImages,
					batchLabels: workerLabels,
					numSamples:  workerSamples,
					done:        make(chan bool, 1),
				}
				workers = append(workers, worker)
				
				// Start processing
				go worker.ProcessBatch()
			}
			
			// Wait for all workers to complete and aggregate results
			batchLoss := 0.0
			batchCorrect := 0
			totalSamples := 0
			for _, worker := range workers {
				<-worker.done
				batchLoss += worker.loss  // Already weighted by samples
				batchCorrect += int(worker.accuracy)  // Raw correct count
				totalSamples += worker.numSamples
			}
			
			// Calculate batch metrics
			if totalSamples > 0 {
				epochLoss += batchLoss / float64(totalSamples)
				epochAcc += float64(batchCorrect) / float64(totalSamples)
			}
			
			// Average gradients across workers and update master model
			pm.AverageGradientsAndUpdate(lr)
			
			// Sync worker models with master
			pm.SyncWorkers()
			
			// Print progress
			if batch%100 == 0 && batch > 0 {
				avgLoss := epochLoss / float64(batch+1)
				avgAcc := epochAcc / float64(batch+1)
				fmt.Printf("Epoch %d, Batch %d/%d, Loss: %.4f, Accuracy: %.2f%%\n", 
					epoch, batch, numBatches, avgLoss, avgAcc*100)
			}
		}
		
		avgLoss := epochLoss / float64(numBatches)
		avgAcc := epochAcc / float64(numBatches)
		epochTime := time.Since(epochStart)
		
		// Evaluate on test set
		testAcc := evaluateModel(pm.master, xTest, yTest, batchSize)
		
		fmt.Printf("Epoch %d Complete - Train Loss: %.4f, Train Acc: %.2f%%, Test Acc: %.2f%%, Time: %v\n", 
			epoch, avgLoss, avgAcc*100, testAcc*100, epochTime)
		
		// Save checkpoint
		if checkpointManager != nil {
			fmt.Printf("Saving checkpoint for epoch %d...\n", epoch)
			if err := checkpointManager.SaveCheckpoint(pm.master.model, epoch, avgLoss); err != nil {
				fmt.Printf("Warning: Failed to save checkpoint: %v\n", err)
			}
		}
		
		// Decay learning rate
		if epoch%5 == 0 {
			lr *= 0.9
			fmt.Printf("Learning rate decayed to: %.6f\n", lr)
		}
	}
}

// AverageGradientsAndUpdate averages gradients across workers and updates master
func (pm *ParallelCNNModel) AverageGradientsAndUpdate(lr float64) {
	// Collect gradients from all worker models
	allGradients := make([][][]*utils.Tensor, pm.numWorkers)
	for i, worker := range pm.models {
		allGradients[i] = worker.GetGradients()
	}
	
	// Average gradients and apply to master model
	masterGrads := pm.master.GetGradients()
	
	// For each layer
	for layerIdx := range masterGrads {
		// For each parameter in the layer (weights, bias)
		for paramIdx := range masterGrads[layerIdx] {
			if masterGrads[layerIdx][paramIdx] == nil {
				// Initialize master gradient tensor if needed
				if len(allGradients) > 0 && len(allGradients[0]) > layerIdx && 
				   len(allGradients[0][layerIdx]) > paramIdx && 
				   allGradients[0][layerIdx][paramIdx] != nil {
					masterGrads[layerIdx][paramIdx] = utils.NewTensor(allGradients[0][layerIdx][paramIdx].Shape...)
				} else {
					continue
				}
			}
			
			// Zero out master gradients
			for i := range masterGrads[layerIdx][paramIdx].Data {
				masterGrads[layerIdx][paramIdx].Data[i] = 0
			}
			
			// Sum gradients from all workers
			for workerIdx := 0; workerIdx < pm.numWorkers; workerIdx++ {
				workerGrad := allGradients[workerIdx][layerIdx][paramIdx]
				if workerGrad != nil {
					for i := range workerGrad.Data {
						masterGrads[layerIdx][paramIdx].Data[i] += workerGrad.Data[i]
					}
				}
			}
			
			// Average the gradients
			for i := range masterGrads[layerIdx][paramIdx].Data {
				masterGrads[layerIdx][paramIdx].Data[i] /= float64(pm.numWorkers)
			}
		}
	}
	
	// Apply averaged gradients to master model using UpdateWeights
	pm.master.UpdateWeights(lr)
	
	// Clear worker gradients for next iteration
	for _, worker := range pm.models {
		worker.ClearGradients()
	}
}

// SyncWorkers synchronizes worker models with master
func (pm *ParallelCNNModel) SyncWorkers() {
	for _, worker := range pm.models {
		worker.CopyWeightsFrom(pm.master)
	}
}

// Helper functions
func relu(x *utils.Tensor) *utils.Tensor {
	output := utils.NewTensor(x.Shape...)
	for i := range x.Data {
		if x.Data[i] > 0 {
			output.Data[i] = x.Data[i]
		}
	}
	return output
}

func reluBackward(grad *utils.Tensor, cache *utils.Tensor) *utils.Tensor {
	output := utils.NewTensor(grad.Shape...)
	for i := range grad.Data {
		if cache.Data[i] > 0 {
			output.Data[i] = grad.Data[i]
		} else {
			output.Data[i] = 0
		}
	}
	return output
}

func crossEntropyLoss(predictions *utils.Tensor, labels []int) (float64, *utils.Tensor) {
	batchSize := predictions.Shape[0]
	classCount := predictions.Shape[1]
	
	probs := predictions
	loss := 0.0
	
	for b := 0; b < batchSize; b++ {
		label := labels[b]
		prob := probs.Data[b*classCount+label]
		if prob > 0 {
			loss -= math.Log(prob)
		}
	}
	loss /= float64(batchSize)
	
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

func accuracy(predictions *utils.Tensor, labels []int) float64 {
	correct := 0
	batchSize := predictions.Shape[0]
	classCount := predictions.Shape[1]
	
	for b := 0; b < batchSize; b++ {
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

func getLabel(yTrain *utils.Tensor, idx int) int {
	maxIdx := 0
	maxVal := yTrain.Data[idx*10]
	for c := 1; c < 10; c++ {
		if yTrain.Data[idx*10+c] > maxVal {
			maxVal = yTrain.Data[idx*10+c]
			maxIdx = c
		}
	}
	return maxIdx
}

// evaluateModel evaluates the model on test data
func evaluateModel(model *CNNModel, xTest, yTest *utils.Tensor, batchSize int) float64 {
	numSamples := xTest.Shape[0]
	numBatches := numSamples / batchSize
	totalAcc := 0.0
	
	for batch := 0; batch < numBatches; batch++ {
		// Prepare batch
		batchImages := utils.NewTensor(batchSize, 1, 28, 28)
		batchLabels := make([]int, batchSize)
		
		for j := 0; j < batchSize; j++ {
			idx := batch*batchSize + j
			if idx >= numSamples {
				break
			}
			// Copy flattened image and reshape to (1, 28, 28)
			for k := 0; k < 784; k++ {
				batchImages.Data[j*784+k] = xTest.Data[idx*784+k]
			}
			// Get label from one-hot encoding
			batchLabels[j] = getLabel(yTest, idx)
		}
		
		// Forward pass (no training)
		predictions := model.Forward(batchImages, false)
		
		// Calculate accuracy
		totalAcc += accuracy(predictions, batchLabels)
	}
	
	return totalAcc / float64(numBatches)
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func main() {
	// Set random seed
	rand.Seed(42)
	
	fmt.Println("=== MNIST CNN Parallel Training ===")
	fmt.Println()
	
	// Determine number of CPU cores
	numCPU := runtime.NumCPU()
	fmt.Printf("System has %d CPU cores\n", numCPU)
	
	// Use half the cores for workers (leave some for system)
	numWorkers := numCPU / 2
	if numWorkers < 2 {
		numWorkers = 2
	}
	fmt.Printf("Using %d parallel workers\n", numWorkers)
	
	// Load MNIST data
	fmt.Println("\nLoading MNIST dataset...")
	startTime := time.Now()
	xTrain, yTrain, xTest, yTest, err := data.LoadVendoredMNISTSubset(10000, 2000) // Reduced dataset for faster training
	if err != nil {
		fmt.Printf("Error loading MNIST data: %v\n", err)
		return
	}
	loadTime := time.Since(startTime)
	
	fmt.Printf("Loaded %d training samples in %v\n",
		xTrain.Shape[0], loadTime)
	
	// Create parallel model
	fmt.Println("\nCreating parallel CNN model...")
	model := NewParallelCNNModel(numWorkers)
	
	// Set up checkpoint manager
	checkpointDir := "models/mnist_cnn_parallel_checkpoints"
	fmt.Printf("Checkpoints will be saved to: %s\n", checkpointDir)
	checkpointManager := inference.NewCheckpointManager(checkpointDir, 3)
	
	// Training parameters
	epochs := 10
	batchSize := 64  // Reduced from 256 to balance speed and accuracy
	learningRate := 0.001
	
	fmt.Printf("\nTraining parameters:\n")
	fmt.Printf("- Epochs: %d\n", epochs)
	fmt.Printf("- Batch Size: %d\n", batchSize)
	fmt.Printf("- Learning Rate: %.4f\n", learningRate)
	fmt.Printf("- Parallel Workers: %d\n", numWorkers)
	fmt.Println()
	
	// Train the model
	fmt.Println("Starting parallel training...")
	trainStart := time.Now()
	TrainParallel(model, xTrain, yTrain, epochs, batchSize, learningRate, checkpointManager, xTest, yTest)
	trainTime := time.Since(trainStart)
	fmt.Printf("\nTotal training time: %v\n", trainTime)
	
	// Compare with sequential time estimate
	sequentialEstimate := trainTime.Seconds() * float64(numWorkers) * 0.8 // Rough estimate
	fmt.Printf("Estimated sequential time: %v\n", time.Duration(sequentialEstimate)*time.Second)
	fmt.Printf("Speedup: %.2fx\n", sequentialEstimate/trainTime.Seconds())
	
	// Save final model
	finalModelPath := "models/mnist_cnn_parallel_final"
	fmt.Printf("\nSaving final model to %s...\n", finalModelPath)
	if err := inference.SaveModel(model.master.model, finalModelPath); err != nil {
		fmt.Printf("Error saving model: %v\n", err)
	} else {
		fmt.Println("Model saved successfully!")
	}
}