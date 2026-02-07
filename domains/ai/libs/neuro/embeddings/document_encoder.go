package embeddings

import (
	"math"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
	"strings"
)

type ChunkingStrategy int

const (
	FixedSizeChunking ChunkingStrategy = iota
	SentenceChunking
	ParagraphChunking
	SlidingWindowChunking
)

type DocumentEncoder struct {
	sentenceEncoder  *SentenceEncoder
	chunkingStrategy ChunkingStrategy
	maxChunkSize     int
	chunkOverlap     int
	aggregationMethod string
	attentionWeights *LinearProjection
}

func NewDocumentEncoder(embeddingDim int, chunkingStrategy ChunkingStrategy) *DocumentEncoder {
	return &DocumentEncoder{
		sentenceEncoder:   NewSentenceEncoder(embeddingDim, MeanPooling),
		chunkingStrategy:  chunkingStrategy,
		maxChunkSize:      512,
		chunkOverlap:      50,
		aggregationMethod: "attention",
		attentionWeights:  NewLinearProjection(embeddingDim, 1),
	}
}

func (de *DocumentEncoder) EncodeDocument(text string) *utils.Tensor {
	chunks := de.chunkDocument(text)
	
	if len(chunks) == 0 {
		return utils.Zeros([]int{1, de.sentenceEncoder.embeddingDim})
	}
	
	chunkEmbeddings := make([]*utils.Tensor, len(chunks))
	for i, chunk := range chunks {
		chunkEmbeddings[i] = de.sentenceEncoder.Encode(chunk)
	}
	
	var aggregated *utils.Tensor
	switch de.aggregationMethod {
	case "attention":
		aggregated = de.attentionAggregation(chunkEmbeddings)
	case "mean":
		aggregated = de.meanAggregation(chunkEmbeddings)
	case "max":
		aggregated = de.maxAggregation(chunkEmbeddings)
	default:
		aggregated = de.attentionAggregation(chunkEmbeddings)
	}
	
	aggregated = de.sentenceEncoder.l2Normalize(aggregated)
	
	return aggregated
}

func (de *DocumentEncoder) chunkDocument(text string) []string {
	switch de.chunkingStrategy {
	case FixedSizeChunking:
		return de.fixedSizeChunks(text)
	case SentenceChunking:
		return de.sentenceChunks(text)
	case ParagraphChunking:
		return de.paragraphChunks(text)
	case SlidingWindowChunking:
		return de.slidingWindowChunks(text)
	default:
		return de.fixedSizeChunks(text)
	}
}

func (de *DocumentEncoder) fixedSizeChunks(text string) []string {
	words := strings.Fields(text)
	var chunks []string
	
	for i := 0; i < len(words); i += de.maxChunkSize {
		end := i + de.maxChunkSize
		if end > len(words) {
			end = len(words)
		}
		chunk := strings.Join(words[i:end], " ")
		chunks = append(chunks, chunk)
	}
	
	return chunks
}

func (de *DocumentEncoder) sentenceChunks(text string) []string {
	sentences := de.splitIntoSentences(text)
	var chunks []string
	currentChunk := ""
	currentSize := 0
	
	for _, sentence := range sentences {
		sentenceWords := strings.Fields(sentence)
		sentenceSize := len(sentenceWords)
		
		if currentSize+sentenceSize > de.maxChunkSize && currentChunk != "" {
			chunks = append(chunks, currentChunk)
			currentChunk = sentence
			currentSize = sentenceSize
		} else {
			if currentChunk != "" {
				currentChunk += " " + sentence
			} else {
				currentChunk = sentence
			}
			currentSize += sentenceSize
		}
	}
	
	if currentChunk != "" {
		chunks = append(chunks, currentChunk)
	}
	
	return chunks
}

func (de *DocumentEncoder) paragraphChunks(text string) []string {
	paragraphs := strings.Split(text, "\n\n")
	var chunks []string
	
	for _, paragraph := range paragraphs {
		paragraph = strings.TrimSpace(paragraph)
		if paragraph == "" {
			continue
		}
		
		words := strings.Fields(paragraph)
		if len(words) > de.maxChunkSize {
			subChunks := de.fixedSizeChunks(paragraph)
			chunks = append(chunks, subChunks...)
		} else {
			chunks = append(chunks, paragraph)
		}
	}
	
	return chunks
}

func (de *DocumentEncoder) slidingWindowChunks(text string) []string {
	words := strings.Fields(text)
	var chunks []string
	
	step := de.maxChunkSize - de.chunkOverlap
	if step <= 0 {
		step = 1
	}
	
	for i := 0; i < len(words); i += step {
		end := i + de.maxChunkSize
		if end > len(words) {
			end = len(words)
		}
		
		chunk := strings.Join(words[i:end], " ")
		chunks = append(chunks, chunk)
		
		if end == len(words) {
			break
		}
	}
	
	return chunks
}

func (de *DocumentEncoder) splitIntoSentences(text string) []string {
	var sentences []string
	var currentSentence strings.Builder
	
	chars := []rune(text)
	for i, char := range chars {
		currentSentence.WriteRune(char)
		
		if char == '.' || char == '!' || char == '?' {
			if i+1 < len(chars) && (chars[i+1] == ' ' || chars[i+1] == '\n') {
				sentence := strings.TrimSpace(currentSentence.String())
				if sentence != "" {
					sentences = append(sentences, sentence)
				}
				currentSentence.Reset()
			}
		}
	}
	
	if currentSentence.Len() > 0 {
		sentence := strings.TrimSpace(currentSentence.String())
		if sentence != "" {
			sentences = append(sentences, sentence)
		}
	}
	
	return sentences
}

func (de *DocumentEncoder) attentionAggregation(embeddings []*utils.Tensor) *utils.Tensor {
	numChunks := len(embeddings)
	embeddingDim := embeddings[0].Shape[1]
	
	allEmbeddings := utils.NewTensor(numChunks, embeddingDim)
	for i, emb := range embeddings {
		for j := 0; j < embeddingDim; j++ {
			allEmbeddings.Data[i*embeddingDim+j] = emb.Data[j]
		}
	}
	
	attentionScores := de.attentionWeights.Forward(allEmbeddings)
	
	attentionWeights := de.softmax(attentionScores)
	
	output := utils.Zeros([]int{1, embeddingDim})
	for i := 0; i < numChunks; i++ {
		weight := attentionWeights.Data[i]
		for j := 0; j < embeddingDim; j++ {
			output.Data[j] += weight * embeddings[i].Data[j]
		}
	}
	
	return output
}

func (de *DocumentEncoder) meanAggregation(embeddings []*utils.Tensor) *utils.Tensor {
	numChunks := len(embeddings)
	embeddingDim := embeddings[0].Shape[1]
	
	output := utils.Zeros([]int{1, embeddingDim})
	
	for _, emb := range embeddings {
		for j := 0; j < embeddingDim; j++ {
			output.Data[j] += emb.Data[j]
		}
	}
	
	for j := 0; j < embeddingDim; j++ {
		output.Data[j] /= float64(numChunks)
	}
	
	return output
}

func (de *DocumentEncoder) maxAggregation(embeddings []*utils.Tensor) *utils.Tensor {
	embeddingDim := embeddings[0].Shape[1]
	
	output := utils.NewTensor(1, embeddingDim)
	
	for j := 0; j < embeddingDim; j++ {
		maxVal := embeddings[0].Data[j]
		for i := 1; i < len(embeddings); i++ {
			if embeddings[i].Data[j] > maxVal {
				maxVal = embeddings[i].Data[j]
			}
		}
		output.Data[j] = maxVal
	}
	
	return output
}

func (de *DocumentEncoder) softmax(x *utils.Tensor) *utils.Tensor {
	maxVal := x.Data[0]
	for i := 1; i < len(x.Data); i++ {
		if x.Data[i] > maxVal {
			maxVal = x.Data[i]
		}
	}
	
	output := utils.NewTensor(x.Shape...)
	sum := 0.0
	
	for i := range x.Data {
		output.Data[i] = math.Exp(x.Data[i] - maxVal)
		sum += output.Data[i]
	}
	
	for i := range output.Data {
		output.Data[i] /= sum
	}
	
	return output
}

func (de *DocumentEncoder) SetMaxChunkSize(size int) {
	de.maxChunkSize = size
}

func (de *DocumentEncoder) SetChunkOverlap(overlap int) {
	de.chunkOverlap = overlap
}

func (de *DocumentEncoder) SetAggregationMethod(method string) {
	de.aggregationMethod = method
}

func (de *DocumentEncoder) GetChunks(text string) []string {
	return de.chunkDocument(text)
}

func (de *DocumentEncoder) EncodeChunks(text string) []*utils.Tensor {
	chunks := de.chunkDocument(text)
	embeddings := make([]*utils.Tensor, len(chunks))
	
	for i, chunk := range chunks {
		embeddings[i] = de.sentenceEncoder.Encode(chunk)
	}
	
	return embeddings
}

type DocumentBatch struct {
	Documents []string
	Metadata  []map[string]interface{}
}

func (de *DocumentEncoder) EncodeBatch(batch *DocumentBatch) []*utils.Tensor {
	embeddings := make([]*utils.Tensor, len(batch.Documents))
	
	for i, doc := range batch.Documents {
		embeddings[i] = de.EncodeDocument(doc)
	}
	
	return embeddings
}

func (de *DocumentEncoder) ComputeSimilarityMatrix(embeddings []*utils.Tensor) *utils.Tensor {
	n := len(embeddings)
	similarity := utils.NewTensor(n, n)
	
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			sim := de.sentenceEncoder.CosineSimilarity(embeddings[i], embeddings[j])
			similarity.Data[i*n+j] = sim
		}
	}
	
	return similarity
}