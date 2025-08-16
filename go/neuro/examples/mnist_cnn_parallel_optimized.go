package main

import (
	"fmt"
	"math"
	"math/rand"
	"runtime"
	"sync"
	"sync/atomic"
	"time"

	"github.com/muchq/moonbase/go/neuro/activations"
	"github.com/muchq/moonbase/go/neuro/data"
	"github.com/muchq/moonbase/go/neuro/inference"
	"github.com/muchq/moonbase/go/neuro/layers"
	"github.com/muchq/moonbase/go/neuro/network"
	"github.com/muchq/moonbase/go/neuro/utils"
)

// TensorPool manages reusable tensor buffers
type TensorPool struct {
	pools map[string]*sync.Pool
	mu    sync.RWMutex
}

func NewTensorPool() *TensorPool {
	return &TensorPool{
		pools: make(map[string]*sync.Pool),
	}
}

func (tp *TensorPool) Get(shape ...int) *utils.Tensor {
	key := fmt.Sprintf("%v", shape)
	
	tp.mu.RLock()
	pool, exists := tp.pools[key]
	tp.mu.RUnlock()
	
	if !exists {
		tp.mu.Lock()
		pool = &sync.Pool{
			New: func() interface{} {
				return utils.NewTensor(shape...)
			},
		}
		tp.pools[key] = pool
		tp.mu.Unlock()
	}
	
	t := pool.Get().(*utils.Tensor)
	// Clear the tensor
	for i := range t.Data {
		t.Data[i] = 0
	}
	return t
}

func (tp *TensorPool) Put(t *utils.Tensor) {
	if t == nil {
		return
	}
	key := fmt.Sprintf("%v", t.Shape)
	
	tp.mu.RLock()
	pool, exists := tp.pools[key]
	tp.mu.RUnlock()
	
	if exists {
		pool.Put(t)
	}
}

// GradientStats tracks gradient statistics for monitoring
type GradientStats struct {
	norm     float64
	min      float64
	max      float64
	mean     float64
	variance float64
}

func computeGradientStats(grad *utils.Tensor) GradientStats {
	if grad == nil || len(grad.Data) == 0 {
		return GradientStats{}
	}
	
	stats := GradientStats{
		min: grad.Data[0],
		max: grad.Data[0],
	}
	
	sum := 0.0
	sumSq := 0.0
	
	for _, v := range grad.Data {
		if v < stats.min {
			stats.min = v
		}
		if v > stats.max {
			stats.max = v
		}
		sum += v
		sumSq += v * v
	}
	
	n := float64(len(grad.Data))
	stats.mean = sum / n
	stats.variance = (sumSq / n) - (stats.mean * stats.mean)
	stats.norm = math.Sqrt(sumSq)
	
	return stats
}

// clipGradients clips gradients by global norm
func clipGradients(grads [][]*utils.Tensor, maxNorm float64) float64 {
	// Compute global norm
	globalNorm := 0.0
	for _, layerGrads := range grads {
		for _, grad := range layerGrads {
			if grad != nil {
				for _, v := range grad.Data {
					globalNorm += v * v
				}
			}
		}
	}
	globalNorm = math.Sqrt(globalNorm)
	
	// Clip if necessary
	if globalNorm > maxNorm {
		scale := maxNorm / globalNorm
		for _, layerGrads := range grads {
			for _, grad := range layerGrads {
				if grad != nil {
					for i := range grad.Data {
						grad.Data[i] *= scale
					}
				}
			}
		}
		return maxNorm
	}
	
	return globalNorm
}

// OptimizedCNNModel represents a memory-efficient CNN
type OptimizedCNNModel struct {
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
	
	// Gradient accumulator (for gradient-only workers)
	gradAccum [][]*utils.Tensor
}

// NewOptimizedCNNModel creates a new CNN model
func NewOptimizedCNNModel(isGradientOnly bool) *OptimizedCNNModel {
	model := &OptimizedCNNModel{}
	
	if !isGradientOnly {
		// Full model with all layers
		model.model = network.NewModel()
		model.conv1 = layers.NewConv2D(1, 32, []int{3, 3}, 1, "valid", true)
		model.pool1 = layers.NewMaxPool2D([]int{2, 2}, 2, "valid")
		model.dropout1 = layers.NewDropout(0.25)
		model.conv2 = layers.NewConv2D(32, 64, []int{3, 3}, 1, "valid", true)
		model.pool2 = layers.NewMaxPool2D([]int{2, 2}, 2, "valid")
		model.dropout2 = layers.NewDropout(0.5)
		model.flatten = layers.NewFlatten()
		model.dense1 = layers.NewDense(1600, 128, &activations.ReLU{})
		model.dense2 = layers.NewDense(128, 10, &activations.Softmax{})
		
		// Add layers to model
		model.model.Add(model.conv1)
		model.model.Add(model.pool1)
		model.model.Add(model.dropout1)
		model.model.Add(model.conv2)
		model.model.Add(model.pool2)
		model.model.Add(model.dropout2)
		model.model.Add(model.flatten)
		model.model.Add(model.dense1)
		model.model.Add(model.dense2)
	} else {
		// Gradient-only model - just accumulate gradients
		model.gradAccum = make([][]*utils.Tensor, 4)
	}
	
	return model
}

// Forward performs forward propagation
func (m *OptimizedCNNModel) Forward(input *utils.Tensor, training bool) *utils.Tensor {
	// Conv block 1
	x := m.conv1.Forward(input, training)
	m.reluCache1 = x.Copy()
	x = relu(x)
	x = m.pool1.Forward(x, training)
	x = m.dropout1.Forward(x, training)
	
	// Conv block 2
	x = m.conv2.Forward(x, training)
	m.reluCache2 = x.Copy()
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
func (m *OptimizedCNNModel) Backward(gradOutput *utils.Tensor) *utils.Tensor {
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

// ParallelTrainer manages optimized parallel training
type ParallelTrainer struct {
	master       *OptimizedCNNModel
	numWorkers   int
	tensorPool   *TensorPool
	batchQueue   chan *BatchData
	resultQueue  chan *BatchResult
	stopSignal   chan struct{}
	wg           sync.WaitGroup
	
	// Gradient clipping
	maxGradNorm  float64
	
	// Statistics
	totalBatches uint64
	totalTime    time.Duration
}

// BatchData represents a batch to process
type BatchData struct {
	id       int
	images   *utils.Tensor
	labels   []int
	training bool
}

// BatchResult contains results from processing a batch
type BatchResult struct {
	id       int
	loss     float64
	accuracy float64
	grads    [][]*utils.Tensor
	gradNorm float64
}

// NewParallelTrainer creates an optimized parallel trainer
func NewParallelTrainer(numWorkers int, maxGradNorm float64) *ParallelTrainer {
	trainer := &ParallelTrainer{
		master:       NewOptimizedCNNModel(false),
		numWorkers:   numWorkers,
		tensorPool:   NewTensorPool(),
		batchQueue:   make(chan *BatchData, numWorkers*2),
		resultQueue:  make(chan *BatchResult, numWorkers),
		stopSignal:   make(chan struct{}),
		maxGradNorm:  maxGradNorm,
	}
	
	// Start worker goroutines
	for i := 0; i < numWorkers; i++ {
		trainer.wg.Add(1)
		go trainer.worker(i)
	}
	
	return trainer
}

// worker processes batches continuously
func (pt *ParallelTrainer) worker(id int) {
	defer pt.wg.Done()
	
	// Each worker has its own model copy
	model := NewOptimizedCNNModel(false)
	
	for {
		select {
		case <-pt.stopSignal:
			return
		case batch := <-pt.batchQueue:
			if batch == nil {
				return
			}
			
			// Sync with master weights
			model.CopyWeightsFrom(pt.master)
			
			// Process batch
			predictions := model.Forward(batch.images, batch.training)
			
			// Compute loss and gradient
			loss, grad := crossEntropyLoss(predictions, batch.labels)
			accuracy := computeAccuracy(predictions, batch.labels)
			
			// Backward pass
			model.Backward(grad)
			
			// Get gradients
			grads := model.GetGradients()
			
			// Clip gradients
			gradNorm := clipGradients(grads, pt.maxGradNorm)
			
			// Send result
			pt.resultQueue <- &BatchResult{
				id:       batch.id,
				loss:     loss,
				accuracy: accuracy,
				grads:    grads,
				gradNorm: gradNorm,
			}
			
			// Return tensor to pool
			pt.tensorPool.Put(batch.images)
		}
	}
}

// ProcessBatch adds a batch to the processing queue
func (pt *ParallelTrainer) ProcessBatch(images *utils.Tensor, labels []int, training bool) {
	batch := &BatchData{
		id:       int(atomic.AddUint64(&pt.totalBatches, 1)),
		images:   images,
		labels:   labels,
		training: training,
	}
	pt.batchQueue <- batch
}

// CollectResults collects and averages results from workers
func (pt *ParallelTrainer) CollectResults(numBatches int, lr float64) (float64, float64, float64) {
	totalLoss := 0.0
	totalAcc := 0.0
	totalGradNorm := 0.0
	
	// Accumulate gradients
	var accumGrads [][]*utils.Tensor
	
	for i := 0; i < numBatches; i++ {
		result := <-pt.resultQueue
		totalLoss += result.loss
		totalAcc += result.accuracy
		totalGradNorm += result.gradNorm
		
		// Accumulate gradients
		if accumGrads == nil {
			accumGrads = make([][]*utils.Tensor, len(result.grads))
			for j := range result.grads {
				accumGrads[j] = make([]*utils.Tensor, len(result.grads[j]))
				for k := range result.grads[j] {
					if result.grads[j][k] != nil {
						accumGrads[j][k] = utils.NewTensor(result.grads[j][k].Shape...)
						copy(accumGrads[j][k].Data, result.grads[j][k].Data)
					}
				}
			}
		} else {
			// Add to accumulated gradients
			for j := range result.grads {
				for k := range result.grads[j] {
					if result.grads[j][k] != nil && accumGrads[j][k] != nil {
						for idx := range result.grads[j][k].Data {
							accumGrads[j][k].Data[idx] += result.grads[j][k].Data[idx]
						}
					}
				}
			}
		}
	}
	
	// Average gradients
	for j := range accumGrads {
		for k := range accumGrads[j] {
			if accumGrads[j][k] != nil {
				for idx := range accumGrads[j][k].Data {
					accumGrads[j][k].Data[idx] /= float64(numBatches)
				}
			}
		}
	}
	
	// Apply gradients to master
	pt.master.ApplyGradients(accumGrads, lr)
	
	return totalLoss / float64(numBatches), 
	       totalAcc / float64(numBatches),
	       totalGradNorm / float64(numBatches)
}

// Stop gracefully stops all workers
func (pt *ParallelTrainer) Stop() {
	close(pt.stopSignal)
	pt.wg.Wait()
	close(pt.batchQueue)
	close(pt.resultQueue)
}

// CopyWeightsFrom copies weights from another model
func (m *OptimizedCNNModel) CopyWeightsFrom(source *OptimizedCNNModel) {
	if m.conv1 != nil && source.conv1 != nil {
		srcParams := source.conv1.GetParams()
		m.conv1.SetParams(srcParams)
	}
	
	if m.conv2 != nil && source.conv2 != nil {
		srcParams := source.conv2.GetParams()
		m.conv2.SetParams(srcParams)
	}
	
	if m.dense1 != nil && source.dense1 != nil {
		srcParams := source.dense1.GetParams()
		m.dense1.SetParams(srcParams)
	}
	
	if m.dense2 != nil && source.dense2 != nil {
		srcParams := source.dense2.GetParams()
		m.dense2.SetParams(srcParams)
	}
}

// GetGradients collects all gradients from the model
func (m *OptimizedCNNModel) GetGradients() [][]*utils.Tensor {
	if m.gradAccum != nil {
		return m.gradAccum
	}
	
	grads := make([][]*utils.Tensor, 4)
	
	if m.conv1 != nil {
		grads[0] = m.conv1.GetGradients()
	}
	if m.conv2 != nil {
		grads[1] = m.conv2.GetGradients()
	}
	if m.dense1 != nil {
		grads[2] = m.dense1.GetGradients()
	}
	if m.dense2 != nil {
		grads[3] = m.dense2.GetGradients()
	}
	
	return grads
}

// ApplyGradients applies gradients with learning rate
func (m *OptimizedCNNModel) ApplyGradients(grads [][]*utils.Tensor, lr float64) {
	// Apply to conv1
	if len(grads) > 0 && m.conv1 != nil {
		params := m.conv1.GetParams()
		for i, grad := range grads[0] {
			if grad != nil && i < len(params) && params[i] != nil {
				for j := range params[i].Data {
					params[i].Data[j] -= lr * grad.Data[j]
				}
			}
		}
	}
	
	// Apply to conv2
	if len(grads) > 1 && m.conv2 != nil {
		params := m.conv2.GetParams()
		for i, grad := range grads[1] {
			if grad != nil && i < len(params) && params[i] != nil {
				for j := range params[i].Data {
					params[i].Data[j] -= lr * grad.Data[j]
				}
			}
		}
	}
	
	// Apply to dense1
	if len(grads) > 2 && m.dense1 != nil {
		params := m.dense1.GetParams()
		for i, grad := range grads[2] {
			if grad != nil && i < len(params) && params[i] != nil {
				for j := range params[i].Data {
					params[i].Data[j] -= lr * grad.Data[j]
				}
			}
		}
	}
	
	// Apply to dense2
	if len(grads) > 3 && m.dense2 != nil {
		params := m.dense2.GetParams()
		for i, grad := range grads[3] {
			if grad != nil && i < len(params) && params[i] != nil {
				for j := range params[i].Data {
					params[i].Data[j] -= lr * grad.Data[j]
				}
			}
		}
	}
}

// UpdateWeights updates all layer weights
func (m *OptimizedCNNModel) UpdateWeights(lr float64) {
	if m.conv1 != nil {
		m.conv1.UpdateWeights(lr)
	}
	if m.conv2 != nil {
		m.conv2.UpdateWeights(lr)
	}
	if m.dense1 != nil {
		m.dense1.UpdateWeights(lr)
	}
	if m.dense2 != nil {
		m.dense2.UpdateWeights(lr)
	}
}

// TrainOptimized trains the model with optimized parallel processing
func TrainOptimized(trainer *ParallelTrainer, xTrain, yTrain *utils.Tensor, 
	epochs int, batchSize int, lr float64, 
	checkpointManager *inference.CheckpointManager, 
	xTest, yTest *utils.Tensor) {
	
	numSamples := xTrain.Shape[0]
	numBatches := numSamples / batchSize
	
	fmt.Printf("Training on %d samples, %d batches per epoch\n", numSamples, numBatches)
	fmt.Printf("Using %d parallel workers with pipeline processing\n", trainer.numWorkers)
	fmt.Printf("Gradient clipping enabled with max norm: %.2f\n", trainer.maxGradNorm)
	
	for epoch := 1; epoch <= epochs; epoch++ {
		epochStart := time.Now()
		
		// Shuffle training data
		perm := rand.Perm(numSamples)
		
		// Pipeline: submit multiple batches at once
		pipelineDepth := trainer.numWorkers * 2
		currentBatch := 0
		
		epochLoss := 0.0
		epochAcc := 0.0
		epochGradNorm := 0.0
		
		for currentBatch < numBatches {
			// Submit batches to pipeline
			submitted := 0
			for i := 0; i < pipelineDepth && currentBatch+i < numBatches; i++ {
				batchStart := (currentBatch + i) * batchSize
				batchEnd := minInt(batchStart+batchSize, numSamples)
				actualBatchSize := batchEnd - batchStart
				
				// Get tensor from pool
				batchImages := trainer.tensorPool.Get(actualBatchSize, 1, 28, 28)
				batchLabels := make([]int, actualBatchSize)
				
				// Fill batch data
				for j := 0; j < actualBatchSize; j++ {
					dataIdx := perm[batchStart+j]
					for k := 0; k < 784; k++ {
						batchImages.Data[j*784+k] = xTrain.Data[dataIdx*784+k]
					}
					batchLabels[j] = getLabel(yTrain, dataIdx)
				}
				
				// Submit batch
				trainer.ProcessBatch(batchImages, batchLabels, true)
				submitted++
			}
			
			// Collect results
			loss, acc, gradNorm := trainer.CollectResults(submitted, lr)
			epochLoss += loss * float64(submitted)
			epochAcc += acc * float64(submitted)
			epochGradNorm += gradNorm * float64(submitted)
			
			currentBatch += submitted
			
			// Print progress
			if currentBatch%100 == 0 && currentBatch > 0 {
				avgLoss := epochLoss / float64(currentBatch)
				avgAcc := epochAcc / float64(currentBatch)
				avgGradNorm := epochGradNorm / float64(currentBatch)
				fmt.Printf("Epoch %d, Batch %d/%d, Loss: %.4f, Acc: %.2f%%, GradNorm: %.4f\n",
					epoch, currentBatch, numBatches, avgLoss, avgAcc*100, avgGradNorm)
			}
		}
		
		avgLoss := epochLoss / float64(numBatches)
		avgAcc := epochAcc / float64(numBatches)
		epochTime := time.Since(epochStart)
		
		// Evaluate on test set
		testAcc := evaluateOptimized(trainer, xTest, yTest, batchSize)
		
		fmt.Printf("Epoch %d Complete - Train Loss: %.4f, Train Acc: %.2f%%, Test Acc: %.2f%%, Time: %v\n",
			epoch, avgLoss, avgAcc*100, testAcc*100, epochTime)
		
		// Save checkpoint
		if checkpointManager != nil {
			fmt.Printf("Saving checkpoint for epoch %d...\n", epoch)
			if err := checkpointManager.SaveCheckpoint(trainer.master.model, epoch, avgLoss); err != nil {
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

// evaluateOptimized evaluates the model on test data
func evaluateOptimized(trainer *ParallelTrainer, xTest, yTest *utils.Tensor, batchSize int) float64 {
	numSamples := xTest.Shape[0]
	numBatches := numSamples / batchSize
	totalAcc := 0.0
	
	for batch := 0; batch < numBatches; batch++ {
		// Prepare batch
		batchImages := trainer.tensorPool.Get(batchSize, 1, 28, 28)
		batchLabels := make([]int, batchSize)
		
		for j := 0; j < batchSize; j++ {
			idx := batch*batchSize + j
			if idx >= numSamples {
				break
			}
			for k := 0; k < 784; k++ {
				batchImages.Data[j*784+k] = xTest.Data[idx*784+k]
			}
			batchLabels[j] = getLabel(yTest, idx)
		}
		
		// Forward pass (no training)
		predictions := trainer.master.Forward(batchImages, false)
		
		// Calculate accuracy
		totalAcc += computeAccuracy(predictions, batchLabels)
		
		// Return tensor to pool
		trainer.tensorPool.Put(batchImages)
	}
	
	return totalAcc / float64(numBatches)
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
		}
	}
	return output
}

func crossEntropyLoss(predictions *utils.Tensor, labels []int) (float64, *utils.Tensor) {
	batchSize := predictions.Shape[0]
	classCount := predictions.Shape[1]
	
	loss := 0.0
	for b := 0; b < batchSize; b++ {
		label := labels[b]
		prob := predictions.Data[b*classCount+label]
		if prob > 0 {
			loss -= math.Log(prob)
		}
	}
	loss /= float64(batchSize)
	
	grad := utils.NewTensor(predictions.Shape...)
	for b := 0; b < batchSize; b++ {
		for c := 0; c < classCount; c++ {
			grad.Data[b*classCount+c] = predictions.Data[b*classCount+c]
			if c == labels[b] {
				grad.Data[b*classCount+c] -= 1.0
			}
			grad.Data[b*classCount+c] /= float64(batchSize)
		}
	}
	
	return loss, grad
}

func computeAccuracy(predictions *utils.Tensor, labels []int) float64 {
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

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func main() {
	rand.Seed(42)
	
	fmt.Println("=== MNIST CNN Optimized Parallel Training ===")
	fmt.Println()
	
	// Determine number of CPU cores
	numCPU := runtime.NumCPU()
	fmt.Printf("System has %d CPU cores\n", numCPU)
	
	// Use most cores for workers
	numWorkers := numCPU - 1
	if numWorkers < 2 {
		numWorkers = 2
	}
	fmt.Printf("Using %d parallel workers\n", numWorkers)
	
	// Load MNIST data
	fmt.Println("\nLoading MNIST dataset...")
	startTime := time.Now()
	xTrain, yTrain, xTest, yTest, err := data.LoadVendoredMNISTSubset(10000, 2000)
	if err != nil {
		fmt.Printf("Error loading MNIST data: %v\n", err)
		return
	}
	loadTime := time.Since(startTime)
	
	fmt.Printf("Loaded %d training samples in %v\n", xTrain.Shape[0], loadTime)
	
	// Create optimized parallel trainer
	fmt.Println("\nCreating optimized parallel trainer...")
	trainer := NewParallelTrainer(numWorkers, 5.0) // max gradient norm of 5.0
	defer trainer.Stop()
	
	// Set up checkpoint manager
	checkpointDir := "models/mnist_cnn_optimized_checkpoints"
	fmt.Printf("Checkpoints will be saved to: %s\n", checkpointDir)
	checkpointManager := inference.NewCheckpointManager(checkpointDir, 3)
	
	// Training parameters
	epochs := 10
	batchSize := 64
	learningRate := 0.001
	
	fmt.Printf("\nTraining parameters:\n")
	fmt.Printf("- Epochs: %d\n", epochs)
	fmt.Printf("- Batch Size: %d\n", batchSize)
	fmt.Printf("- Learning Rate: %.4f\n", learningRate)
	fmt.Printf("- Parallel Workers: %d\n", numWorkers)
	fmt.Printf("- Pipeline Processing: Enabled\n")
	fmt.Printf("- Memory Pooling: Enabled\n")
	fmt.Printf("- Gradient Clipping: Enabled (max norm: 5.0)\n")
	fmt.Println()
	
	// Train the model
	fmt.Println("Starting optimized parallel training...")
	trainStart := time.Now()
	TrainOptimized(trainer, xTrain, yTrain, epochs, batchSize, learningRate, checkpointManager, xTest, yTest)
	trainTime := time.Since(trainStart)
	fmt.Printf("\nTotal training time: %v\n", trainTime)
	
	// Save final model
	finalModelPath := "models/mnist_cnn_optimized_final"
	fmt.Printf("\nSaving final model to %s...\n", finalModelPath)
	if err := inference.SaveModel(trainer.master.model, finalModelPath); err != nil {
		fmt.Printf("Error saving model: %v\n", err)
	} else {
		fmt.Println("Model saved successfully!")
	}
}