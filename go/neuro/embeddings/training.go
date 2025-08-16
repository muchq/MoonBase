package embeddings

import (
	"math"
	"math/rand"
	"github.com/muchq/moonbase/go/neuro/utils"
)

type TrainingObjective interface {
	ComputeLoss(predictions, targets *utils.Tensor) (*utils.Tensor, *utils.Tensor)
	Name() string
}

type ContrastiveLoss struct {
	temperature float64
	margin      float64
}

func NewContrastiveLoss(temperature float64) *ContrastiveLoss {
	return &ContrastiveLoss{
		temperature: temperature,
		margin:      0.5,
	}
}

func (cl *ContrastiveLoss) ComputeLoss(embeddings *utils.Tensor, labels *utils.Tensor) (*utils.Tensor, *utils.Tensor) {
	batchSize := embeddings.Shape[0]
	embeddingDim := embeddings.Shape[1]
	
	embeddings = normalizeEmbeddings(embeddings)
	
	similarity := utils.MatMul(embeddings, utils.Transpose(embeddings, []int{1, 0}))
	
	similarity = utils.Scale(similarity, 1.0/cl.temperature)
	
	expSim := utils.NewTensor(similarity.Shape...)
	for i := range similarity.Data {
		expSim.Data[i] = math.Exp(similarity.Data[i])
	}
	
	loss := 0.0
	gradients := utils.Zeros(embeddings.Shape)
	
	for i := 0; i < batchSize; i++ {
		positives := []int{}
		negatives := []int{}
		
		for j := 0; j < batchSize; j++ {
			if i == j {
				continue
			}
			if labels.Data[i] == labels.Data[j] {
				positives = append(positives, j)
			} else {
				negatives = append(negatives, j)
			}
		}
		
		if len(positives) == 0 || len(negatives) == 0 {
			continue
		}
		
		posSum := 0.0
		for _, j := range positives {
			posSum += expSim.Data[i*batchSize+j]
		}
		
		negSum := 0.0
		for _, j := range negatives {
			negSum += expSim.Data[i*batchSize+j]
		}
		
		if posSum > 0 && negSum > 0 {
			for _, j := range positives {
				loss -= math.Log(expSim.Data[i*batchSize+j] / (posSum + negSum))
			}
		}
		
		for d := 0; d < embeddingDim; d++ {
			grad := 0.0
			for _, j := range positives {
				grad -= embeddings.Data[j*embeddingDim+d] / cl.temperature
			}
			for _, j := range negatives {
				weight := expSim.Data[i*batchSize+j] / (posSum + negSum)
				grad += weight * embeddings.Data[j*embeddingDim+d] / cl.temperature
			}
			gradients.Data[i*embeddingDim+d] = grad
		}
	}
	
	lossT := utils.NewTensorFromData([]float64{loss / float64(batchSize)}, 1)
	
	return lossT, gradients
}

func (cl *ContrastiveLoss) Name() string {
	return "ContrastiveLoss"
}

type TripletLoss struct {
	margin float64
}

func NewTripletLoss(margin float64) *TripletLoss {
	return &TripletLoss{
		margin: margin,
	}
}

func (tl *TripletLoss) ComputeLoss(anchors, positives, negatives *utils.Tensor) (*utils.Tensor, *utils.Tensor) {
	batchSize := anchors.Shape[0]
	embeddingDim := anchors.Shape[1]
	
	posDistance := euclideanDistance(anchors, positives)
	negDistance := euclideanDistance(anchors, negatives)
	
	loss := 0.0
	gradients := utils.Zeros(anchors.Shape)
	
	for i := 0; i < batchSize; i++ {
		tripletLoss := posDistance.Data[i] - negDistance.Data[i] + tl.margin
		
		if tripletLoss > 0 {
			loss += tripletLoss
			
			for d := 0; d < embeddingDim; d++ {
				anchorIdx := i*embeddingDim + d
				posIdx := i*embeddingDim + d
				negIdx := i*embeddingDim + d
				
				gradients.Data[anchorIdx] += 2 * (positives.Data[posIdx] - negatives.Data[negIdx])
			}
		}
	}
	
	lossT := utils.NewTensorFromData([]float64{loss / float64(batchSize)}, 1)
	
	return lossT, gradients
}

func (tl *TripletLoss) Name() string {
	return "TripletLoss"
}

type MaskedLanguageModel struct {
	vocabSize   int
	maskToken   int
	maskProb    float64
}

func NewMaskedLanguageModel(vocabSize int, maskProb float64) *MaskedLanguageModel {
	return &MaskedLanguageModel{
		vocabSize: vocabSize,
		maskToken: 4,
		maskProb:  maskProb,
	}
}

func (mlm *MaskedLanguageModel) CreateMaskedInput(inputIDs *utils.Tensor) (*utils.Tensor, *utils.Tensor) {
	batchSize := inputIDs.Shape[0]
	seqLen := inputIDs.Shape[1]
	
	maskedInput := utils.NewTensor(inputIDs.Shape...)
	copy(maskedInput.Data, inputIDs.Data)
	
	labels := utils.NewTensor(inputIDs.Shape...)
	for i := range labels.Data {
		labels.Data[i] = -100
	}
	
	for b := 0; b < batchSize; b++ {
		for s := 0; s < seqLen; s++ {
			idx := b*seqLen + s
			
			if inputIDs.Data[idx] <= 4 {
				continue
			}
			
			if rand.Float64() < mlm.maskProb {
				labels.Data[idx] = inputIDs.Data[idx]
				
				r := rand.Float64()
				if r < 0.8 {
					maskedInput.Data[idx] = float64(mlm.maskToken)
				} else if r < 0.9 {
					maskedInput.Data[idx] = float64(rand.Intn(mlm.vocabSize))
				}
			}
		}
	}
	
	return maskedInput, labels
}

func (mlm *MaskedLanguageModel) ComputeLoss(predictions, labels *utils.Tensor) (*utils.Tensor, *utils.Tensor) {
	batchSize := predictions.Shape[0]
	seqLen := predictions.Shape[1]
	vocabSize := predictions.Shape[2]
	
	loss := 0.0
	gradients := utils.Zeros(predictions.Shape)
	count := 0
	
	for b := 0; b < batchSize; b++ {
		for s := 0; s < seqLen; s++ {
			labelIdx := b*seqLen + s
			
			if labels.Data[labelIdx] < 0 {
				continue
			}
			
			trueLabel := int(labels.Data[labelIdx])
			
			maxLogit := -math.MaxFloat64
			for v := 0; v < vocabSize; v++ {
				predIdx := b*seqLen*vocabSize + s*vocabSize + v
				if predictions.Data[predIdx] > maxLogit {
					maxLogit = predictions.Data[predIdx]
				}
			}
			
			sumExp := 0.0
			for v := 0; v < vocabSize; v++ {
				predIdx := b*seqLen*vocabSize + s*vocabSize + v
				sumExp += math.Exp(predictions.Data[predIdx] - maxLogit)
			}
			
			predIdx := b*seqLen*vocabSize + s*vocabSize + trueLabel
			loss -= math.Log(math.Exp(predictions.Data[predIdx]-maxLogit) / sumExp)
			count++
			
			for v := 0; v < vocabSize; v++ {
				predIdx := b*seqLen*vocabSize + s*vocabSize + v
				prob := math.Exp(predictions.Data[predIdx]-maxLogit) / sumExp
				
				if v == trueLabel {
					gradients.Data[predIdx] = prob - 1.0
				} else {
					gradients.Data[predIdx] = prob
				}
			}
		}
	}
	
	if count > 0 {
		loss /= float64(count)
		for i := range gradients.Data {
			gradients.Data[i] /= float64(count)
		}
	}
	
	lossT := utils.NewTensorFromData([]float64{loss}, 1)
	
	return lossT, gradients
}

func (mlm *MaskedLanguageModel) Name() string {
	return "MaskedLanguageModel"
}

type SemanticTextualSimilarity struct {
	marginRanking float64
	scale         float64
}

func NewSemanticTextualSimilarity() *SemanticTextualSimilarity {
	return &SemanticTextualSimilarity{
		marginRanking: 0.5,
		scale:         5.0,
	}
}

func (sts *SemanticTextualSimilarity) ComputeLoss(embeddings1, embeddings2, scores *utils.Tensor) (*utils.Tensor, *utils.Tensor) {
	batchSize := embeddings1.Shape[0]
	
	embeddings1 = normalizeEmbeddings(embeddings1)
	embeddings2 = normalizeEmbeddings(embeddings2)
	
	cosineScores := utils.NewTensor(batchSize)
	for i := 0; i < batchSize; i++ {
		dot := 0.0
		for j := 0; j < embeddings1.Shape[1]; j++ {
			dot += embeddings1.Data[i*embeddings1.Shape[1]+j] * embeddings2.Data[i*embeddings2.Shape[1]+j]
		}
		cosineScores.Data[i] = dot
	}
	
	predictions := utils.Scale(cosineScores, sts.scale)
	
	targets := utils.Scale(scores, 1.0/5.0)
	
	loss := 0.0
	grad1 := utils.Zeros(embeddings1.Shape)
	grad2 := utils.Zeros(embeddings2.Shape)
	
	for i := 0; i < batchSize; i++ {
		diff := predictions.Data[i] - targets.Data[i]
		loss += diff * diff
		
		gradScale := 2 * diff * sts.scale / float64(batchSize)
		
		for j := 0; j < embeddings1.Shape[1]; j++ {
			grad1.Data[i*embeddings1.Shape[1]+j] = gradScale * embeddings2.Data[i*embeddings2.Shape[1]+j]
			grad2.Data[i*embeddings2.Shape[1]+j] = gradScale * embeddings1.Data[i*embeddings1.Shape[1]+j]
		}
	}
	
	lossT := utils.NewTensorFromData([]float64{loss / float64(batchSize)}, 1)
	
	return lossT, grad1
}

func (sts *SemanticTextualSimilarity) Name() string {
	return "SemanticTextualSimilarity"
}

type HardNegativeMiner struct {
	numHardNegatives int
	semiHardMargin   float64
}

func NewHardNegativeMiner(numHardNegatives int) *HardNegativeMiner {
	return &HardNegativeMiner{
		numHardNegatives: numHardNegatives,
		semiHardMargin:   0.1,
	}
}

func (hnm *HardNegativeMiner) Mine(anchors, candidates *utils.Tensor, labels *utils.Tensor) []int {
	batchSize := anchors.Shape[0]
	numCandidates := candidates.Shape[0]
	
	hardNegatives := make([]int, 0, batchSize*hnm.numHardNegatives)
	
	for i := 0; i < batchSize; i++ {
		anchorLabel := labels.Data[i]
		
		type distanceIndex struct {
			distance float64
			index    int
		}
		
		negDistances := []distanceIndex{}
		
		for j := 0; j < numCandidates; j++ {
			if labels.Data[j] == anchorLabel {
				continue
			}
			
			dist := 0.0
			for k := 0; k < anchors.Shape[1]; k++ {
				diff := anchors.Data[i*anchors.Shape[1]+k] - candidates.Data[j*candidates.Shape[1]+k]
				dist += diff * diff
			}
			dist = math.Sqrt(dist)
			
			negDistances = append(negDistances, distanceIndex{distance: dist, index: j})
		}
		
		for k := 0; k < len(negDistances)-1; k++ {
			for l := k + 1; l < len(negDistances); l++ {
				if negDistances[k].distance > negDistances[l].distance {
					negDistances[k], negDistances[l] = negDistances[l], negDistances[k]
				}
			}
		}
		
		numToSelect := hnm.numHardNegatives
		if numToSelect > len(negDistances) {
			numToSelect = len(negDistances)
		}
		
		for k := 0; k < numToSelect; k++ {
			hardNegatives = append(hardNegatives, negDistances[k].index)
		}
	}
	
	return hardNegatives
}

func normalizeEmbeddings(embeddings *utils.Tensor) *utils.Tensor {
	batchSize := embeddings.Shape[0]
	embeddingDim := embeddings.Shape[1]
	
	normalized := utils.NewTensor(embeddings.Shape...)
	
	for i := 0; i < batchSize; i++ {
		norm := 0.0
		for j := 0; j < embeddingDim; j++ {
			val := embeddings.Data[i*embeddingDim+j]
			norm += val * val
		}
		norm = math.Sqrt(norm)
		
		if norm > 0 {
			for j := 0; j < embeddingDim; j++ {
				normalized.Data[i*embeddingDim+j] = embeddings.Data[i*embeddingDim+j] / norm
			}
		}
	}
	
	return normalized
}

func euclideanDistance(x, y *utils.Tensor) *utils.Tensor {
	batchSize := x.Shape[0]
	embeddingDim := x.Shape[1]
	
	distances := utils.NewTensor(batchSize)
	
	for i := 0; i < batchSize; i++ {
		dist := 0.0
		for j := 0; j < embeddingDim; j++ {
			diff := x.Data[i*embeddingDim+j] - y.Data[i*embeddingDim+j]
			dist += diff * diff
		}
		distances.Data[i] = math.Sqrt(dist)
	}
	
	return distances
}

type EmbeddingTrainer struct {
	model            *SentenceEncoder
	optimizer        *AdamOptimizer
	objectives       []TrainingObjective
	hardNegativeMiner *HardNegativeMiner
}

func NewEmbeddingTrainer(model *SentenceEncoder, learningRate float64) *EmbeddingTrainer {
	return &EmbeddingTrainer{
		model:            model,
		optimizer:        NewAdamOptimizer(learningRate),
		objectives:       []TrainingObjective{},
		hardNegativeMiner: NewHardNegativeMiner(5),
	}
}

func (et *EmbeddingTrainer) AddObjective(objective TrainingObjective) {
	et.objectives = append(et.objectives, objective)
}

type AdamOptimizer struct {
	learningRate float64
	beta1        float64
	beta2        float64
	epsilon      float64
	t            int
	m            map[string]*utils.Tensor
	v            map[string]*utils.Tensor
}

func NewAdamOptimizer(learningRate float64) *AdamOptimizer {
	return &AdamOptimizer{
		learningRate: learningRate,
		beta1:        0.9,
		beta2:        0.999,
		epsilon:      1e-8,
		t:            0,
		m:            make(map[string]*utils.Tensor),
		v:            make(map[string]*utils.Tensor),
	}
}