package data

import (
	"math/rand"
	
	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
)

type Dataset struct {
	X         *utils.Tensor
	Y         *utils.Tensor
	BatchSize int
	Shuffle   bool
	indices   []int
	position  int
}

func NewDataset(x, y *utils.Tensor, batchSize int, shuffle bool) *Dataset {
	numSamples := x.Shape[0]
	indices := make([]int, numSamples)
	for i := range indices {
		indices[i] = i
	}
	
	d := &Dataset{
		X:         x,
		Y:         y,
		BatchSize: batchSize,
		Shuffle:   shuffle,
		indices:   indices,
		position:  0,
	}
	
	if shuffle {
		d.shuffleIndices()
	}
	
	return d
}

func (d *Dataset) shuffleIndices() {
	for i := len(d.indices) - 1; i > 0; i-- {
		j := rand.Intn(i + 1)
		d.indices[i], d.indices[j] = d.indices[j], d.indices[i]
	}
}

func (d *Dataset) Reset() {
	d.position = 0
	if d.Shuffle {
		d.shuffleIndices()
	}
}

func (d *Dataset) HasNext() bool {
	return d.position < len(d.indices)
}

func (d *Dataset) NextBatch() (*utils.Tensor, *utils.Tensor) {
	if !d.HasNext() {
		return nil, nil
	}
	
	endPos := d.position + d.BatchSize
	if endPos > len(d.indices) {
		endPos = len(d.indices)
	}
	
	batchIndices := d.indices[d.position:endPos]
	batchSize := len(batchIndices)
	
	xShape := append([]int{batchSize}, d.X.Shape[1:]...)
	yShape := append([]int{batchSize}, d.Y.Shape[1:]...)
	
	xBatch := utils.NewTensor(xShape...)
	yBatch := utils.NewTensor(yShape...)
	
	xStride := len(d.X.Data) / d.X.Shape[0]
	yStride := len(d.Y.Data) / d.Y.Shape[0]
	
	for i, idx := range batchIndices {
		xStart := idx * xStride
		xEnd := xStart + xStride
		yStart := idx * yStride
		yEnd := yStart + yStride
		
		copy(xBatch.Data[i*xStride:(i+1)*xStride], d.X.Data[xStart:xEnd])
		copy(yBatch.Data[i*yStride:(i+1)*yStride], d.Y.Data[yStart:yEnd])
	}
	
	d.position = endPos
	
	return xBatch, yBatch
}

func (d *Dataset) NumBatches() int {
	return (len(d.indices) + d.BatchSize - 1) / d.BatchSize
}

func (d *Dataset) NumSamples() int {
	return len(d.indices)
}

func OneHotEncode(labels []int, numClasses int) *utils.Tensor {
	numSamples := len(labels)
	encoded := utils.NewTensor(numSamples, numClasses)
	
	for i, label := range labels {
		if label >= 0 && label < numClasses {
			encoded.Set(1.0, i, label)
		}
	}
	
	return encoded
}

func Normalize(data *utils.Tensor) (*utils.Tensor, float64, float64) {
	mean := data.Mean()
	
	variance := 0.0
	for _, val := range data.Data {
		diff := val - mean
		variance += diff * diff
	}
	variance /= float64(len(data.Data))
	std := utils.Sqrt(variance)
	
	if std == 0 {
		std = 1.0
	}
	
	normalized := data.Copy()
	for i := range normalized.Data {
		normalized.Data[i] = (normalized.Data[i] - mean) / std
	}
	
	return normalized, mean, std
}

func MinMaxScale(data *utils.Tensor) (*utils.Tensor, float64, float64) {
	if len(data.Data) == 0 {
		return data.Copy(), 0, 1
	}
	
	min := data.Data[0]
	max := data.Data[0]
	
	for _, val := range data.Data {
		if val < min {
			min = val
		}
		if val > max {
			max = val
		}
	}
	
	scale := max - min
	if scale == 0 {
		scale = 1
	}
	
	scaled := data.Copy()
	for i := range scaled.Data {
		scaled.Data[i] = (scaled.Data[i] - min) / scale
	}
	
	return scaled, min, max
}

func TrainTestSplit(x, y *utils.Tensor, testRatio float64) (*utils.Tensor, *utils.Tensor, *utils.Tensor, *utils.Tensor) {
	numSamples := x.Shape[0]
	numTest := int(float64(numSamples) * testRatio)
	numTrain := numSamples - numTest
	
	indices := make([]int, numSamples)
	for i := range indices {
		indices[i] = i
	}
	
	for i := len(indices) - 1; i > 0; i-- {
		j := rand.Intn(i + 1)
		indices[i], indices[j] = indices[j], indices[i]
	}
	
	trainIndices := indices[:numTrain]
	testIndices := indices[numTrain:]
	
	xTrain := extractSamples(x, trainIndices)
	yTrain := extractSamples(y, trainIndices)
	xTest := extractSamples(x, testIndices)
	yTest := extractSamples(y, testIndices)
	
	return xTrain, yTrain, xTest, yTest
}

func extractSamples(data *utils.Tensor, indices []int) *utils.Tensor {
	numSamples := len(indices)
	shape := append([]int{numSamples}, data.Shape[1:]...)
	result := utils.NewTensor(shape...)
	
	stride := len(data.Data) / data.Shape[0]
	
	for i, idx := range indices {
		srcStart := idx * stride
		srcEnd := srcStart + stride
		dstStart := i * stride
		dstEnd := dstStart + stride
		
		copy(result.Data[dstStart:dstEnd], data.Data[srcStart:srcEnd])
	}
	
	return result
}