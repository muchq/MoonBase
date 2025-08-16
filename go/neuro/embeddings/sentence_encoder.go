package embeddings

import (
	"fmt"
	"math"
	"github.com/muchq/moonbase/go/neuro/utils"
)

type PoolingStrategy int

const (
	MeanPooling PoolingStrategy = iota
	MaxPooling
	CLSPooling
)

type SentenceEncoder struct {
	embeddingDim     int
	hiddenDim        int
	numLayers        int
	numHeads         int
	maxSeqLength     int
	vocabSize        int
	poolingStrategy  PoolingStrategy
	tokenizer        Tokenizer
	
	tokenEmbedding   *TokenEmbedding
	positionalEncoder *PositionalEncoding
	transformerBlocks []*TransformerBlock
	layerNorm        *LayerNorm
	outputProjection *LinearProjection
}

func NewSentenceEncoder(embeddingDim int, poolingStrategy PoolingStrategy) *SentenceEncoder {
	vocabSize := 30000
	hiddenDim := 768
	numLayers := 6
	numHeads := 8
	maxSeqLength := 512
	dFF := hiddenDim * 4
	dropout := 0.1
	
	if embeddingDim != 384 && embeddingDim != 768 && embeddingDim != 1024 {
		panic("embeddingDim must be 384, 768, or 1024 for pgvector compatibility")
	}
	
	encoder := &SentenceEncoder{
		embeddingDim:    embeddingDim,
		hiddenDim:       hiddenDim,
		numLayers:       numLayers,
		numHeads:        numHeads,
		maxSeqLength:    maxSeqLength,
		vocabSize:       vocabSize,
		poolingStrategy: poolingStrategy,
		tokenizer:       NewSimpleTokenizer(true),
		
		tokenEmbedding:   NewTokenEmbedding(vocabSize, hiddenDim),
		positionalEncoder: NewPositionalEncoding(hiddenDim, maxSeqLength),
		transformerBlocks: make([]*TransformerBlock, numLayers),
		layerNorm:        NewLayerNorm(hiddenDim),
		outputProjection: NewLinearProjection(hiddenDim, embeddingDim),
	}
	
	for i := 0; i < numLayers; i++ {
		encoder.transformerBlocks[i] = NewTransformerBlock(hiddenDim, numHeads, dFF, dropout)
	}
	
	return encoder
}

func (se *SentenceEncoder) Encode(text string) *utils.Tensor {
	texts := []string{text}
	return se.EncodeBatch(texts)
}

func (se *SentenceEncoder) EncodeBatch(texts []string) *utils.Tensor {
	batchSize := len(texts)
	
	tokenizedBatch := make([][]string, batchSize)
	maxLen := 0
	
	for i, text := range texts {
		tokens := se.tokenizer.Tokenize(text)
		if len(tokens) > se.maxSeqLength-2 {
			tokens = tokens[:se.maxSeqLength-2]
		}
		tokenizedBatch[i] = tokens
		if len(tokens) > maxLen {
			maxLen = len(tokens)
		}
	}
	
	maxLen += 2
	
	inputIDs := utils.Zeros([]int{batchSize, maxLen})
	attentionMask := utils.Zeros([]int{batchSize, maxLen})
	
	for i, tokens := range tokenizedBatch {
		tokens = append([]string{"[CLS]"}, tokens...)
		tokens = append(tokens, "[SEP]")
		
		ids := se.tokenizer.Encode(tokens)
		
		for j, id := range ids {
			inputIDs.Data[i*maxLen+j] = float64(id)
			attentionMask.Data[i*maxLen+j] = 1.0
		}
	}
	
	embeddings := se.tokenEmbedding.Forward(inputIDs)
	
	embeddings = se.positionalEncoder.Forward(embeddings)
	
	hiddenStates := embeddings
	for _, block := range se.transformerBlocks {
		hiddenStates = block.Forward(hiddenStates, attentionMask, false)
	}
	
	hiddenStates = se.layerNorm.Forward(hiddenStates)
	
	var pooledOutput *utils.Tensor
	switch se.poolingStrategy {
	case MeanPooling:
		pooledOutput = se.meanPool(hiddenStates, attentionMask)
	case MaxPooling:
		pooledOutput = se.maxPool(hiddenStates, attentionMask)
	case CLSPooling:
		pooledOutput = se.clsPool(hiddenStates)
	default:
		pooledOutput = se.meanPool(hiddenStates, attentionMask)
	}
	
	output := se.outputProjection.Forward(pooledOutput)
	
	output = se.l2Normalize(output)
	
	return output
}

func (se *SentenceEncoder) meanPool(hiddenStates, attentionMask *utils.Tensor) *utils.Tensor {
	batchSize := hiddenStates.Shape[0]
	seqLen := hiddenStates.Shape[1]
	hiddenDim := hiddenStates.Shape[2]
	
	output := utils.Zeros([]int{batchSize, hiddenDim})
	
	for b := 0; b < batchSize; b++ {
		count := 0.0
		for s := 0; s < seqLen; s++ {
			if attentionMask.Data[b*seqLen+s] > 0 {
				count++
				for h := 0; h < hiddenDim; h++ {
					idx := b*seqLen*hiddenDim + s*hiddenDim + h
					output.Data[b*hiddenDim+h] += hiddenStates.Data[idx]
				}
			}
		}
		
		if count > 0 {
			for h := 0; h < hiddenDim; h++ {
				output.Data[b*hiddenDim+h] /= count
			}
		}
	}
	
	return output
}

func (se *SentenceEncoder) maxPool(hiddenStates, attentionMask *utils.Tensor) *utils.Tensor {
	batchSize := hiddenStates.Shape[0]
	seqLen := hiddenStates.Shape[1]
	hiddenDim := hiddenStates.Shape[2]
	
	output := utils.NewTensor(batchSize, hiddenDim)
	
	for b := 0; b < batchSize; b++ {
		for h := 0; h < hiddenDim; h++ {
			maxVal := -math.MaxFloat64
			for s := 0; s < seqLen; s++ {
				if attentionMask.Data[b*seqLen+s] > 0 {
					idx := b*seqLen*hiddenDim + s*hiddenDim + h
					if hiddenStates.Data[idx] > maxVal {
						maxVal = hiddenStates.Data[idx]
					}
				}
			}
			output.Data[b*hiddenDim+h] = maxVal
		}
	}
	
	return output
}

func (se *SentenceEncoder) clsPool(hiddenStates *utils.Tensor) *utils.Tensor {
	batchSize := hiddenStates.Shape[0]
	hiddenDim := hiddenStates.Shape[2]
	
	output := utils.NewTensor(batchSize, hiddenDim)
	
	for b := 0; b < batchSize; b++ {
		for h := 0; h < hiddenDim; h++ {
			idx := b*hiddenStates.Shape[1]*hiddenDim + h
			output.Data[b*hiddenDim+h] = hiddenStates.Data[idx]
		}
	}
	
	return output
}

func (se *SentenceEncoder) l2Normalize(x *utils.Tensor) *utils.Tensor {
	batchSize := x.Shape[0]
	embeddingDim := x.Shape[1]
	
	output := utils.NewTensor(x.Shape...)
	
	for b := 0; b < batchSize; b++ {
		norm := 0.0
		for e := 0; e < embeddingDim; e++ {
			val := x.Data[b*embeddingDim+e]
			norm += val * val
		}
		norm = math.Sqrt(norm)
		
		if norm > 0 {
			for e := 0; e < embeddingDim; e++ {
				output.Data[b*embeddingDim+e] = x.Data[b*embeddingDim+e] / norm
			}
		}
	}
	
	return output
}

func (se *SentenceEncoder) CosineSimilarity(embedding1, embedding2 *utils.Tensor) float64 {
	if len(embedding1.Data) != len(embedding2.Data) {
		panic("embeddings must have the same dimension")
	}
	
	dotProduct := 0.0
	for i := range embedding1.Data {
		dotProduct += embedding1.Data[i] * embedding2.Data[i]
	}
	
	return dotProduct
}

func (se *SentenceEncoder) GetEmbeddingDim() int {
	return se.embeddingDim
}

func (se *SentenceEncoder) SetTokenizer(tokenizer Tokenizer) {
	se.tokenizer = tokenizer
}

type TokenEmbedding struct {
	vocabSize    int
	embeddingDim int
	embeddings   *utils.Tensor
}

func NewTokenEmbedding(vocabSize, embeddingDim int) *TokenEmbedding {
	embeddings := utils.XavierUniform([]int{vocabSize, embeddingDim})
	
	return &TokenEmbedding{
		vocabSize:    vocabSize,
		embeddingDim: embeddingDim,
		embeddings:   embeddings,
	}
}

func (te *TokenEmbedding) Forward(inputIDs *utils.Tensor) *utils.Tensor {
	batchSize := inputIDs.Shape[0]
	seqLen := inputIDs.Shape[1]
	
	output := utils.NewTensor(batchSize, seqLen, te.embeddingDim)
	
	for b := 0; b < batchSize; b++ {
		for s := 0; s < seqLen; s++ {
			tokenID := int(inputIDs.Data[b*seqLen+s])
			
			if tokenID >= te.vocabSize {
				tokenID = 1
			}
			
			for e := 0; e < te.embeddingDim; e++ {
				outputIdx := b*seqLen*te.embeddingDim + s*te.embeddingDim + e
				embeddingIdx := tokenID*te.embeddingDim + e
				output.Data[outputIdx] = te.embeddings.Data[embeddingIdx]
			}
		}
	}
	
	return output
}

func (se *SentenceEncoder) Save(path string) error {
	return fmt.Errorf("model saving not yet implemented")
}

func (se *SentenceEncoder) Load(path string) error {
	return fmt.Errorf("model loading not yet implemented")
}