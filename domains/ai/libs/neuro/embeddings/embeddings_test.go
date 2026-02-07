package embeddings

import (
	"math"
	"github.com/muchq/moonbase/domains/ai/libs/neuro/utils"
	"strings"
	"testing"
)

func TestSimpleTokenizer(t *testing.T) {
	tokenizer := NewSimpleTokenizer(true)
	
	t.Run("BasicTokenization", func(t *testing.T) {
		text := "Hello, world! This is a test."
		tokens := tokenizer.Tokenize(text)
		
		expected := []string{"hello", ",", "world", "!", "this", "test", "."}
		if len(tokens) != len(expected) {
			t.Errorf("Expected %d tokens, got %d", len(expected), len(tokens))
		}
		
		for i, token := range tokens {
			if i < len(expected) && token != expected[i] {
				t.Errorf("Token %d: expected %s, got %s", i, expected[i], token)
			}
		}
	})
	
	t.Run("EncodeDecode", func(t *testing.T) {
		tokens := []string{"hello", "world", "test"}
		ids := tokenizer.Encode(tokens)
		decoded := tokenizer.Decode(ids)
		
		for i, token := range decoded {
			if token != tokens[i] {
				t.Errorf("Token %d: expected %s, got %s", i, tokens[i], token)
			}
		}
	})
	
	t.Run("SpecialTokens", func(t *testing.T) {
		ids := tokenizer.Encode([]string{"[PAD]", "[UNK]", "[CLS]", "[SEP]"})
		expected := []int{0, 1, 2, 3}
		
		for i, id := range ids {
			if id != expected[i] {
				t.Errorf("Special token %d: expected ID %d, got %d", i, expected[i], id)
			}
		}
	})
}

func TestWordPieceTokenizer(t *testing.T) {
	tokenizer, err := NewWordPieceTokenizer("")
	if err != nil {
		t.Fatalf("Failed to create tokenizer: %v", err)
	}
	
	t.Run("SubwordTokenization", func(t *testing.T) {
		text := "testing"
		tokens := tokenizer.Tokenize(text)
		
		if len(tokens) == 0 {
			t.Error("Expected at least one token")
		}
	})
	
	t.Run("AddSpecialTokens", func(t *testing.T) {
		tokens := []string{"hello", "world"}
		withSpecial := tokenizer.AddSpecialTokens(tokens)
		
		if withSpecial[0] != "[CLS]" {
			t.Errorf("Expected [CLS] at start, got %s", withSpecial[0])
		}
		
		if withSpecial[len(withSpecial)-1] != "[SEP]" {
			t.Errorf("Expected [SEP] at end, got %s", withSpecial[len(withSpecial)-1])
		}
	})
}

func TestMultiHeadAttention(t *testing.T) {
	numHeads := 4
	dModel := 128
	seqLen := 10
	batchSize := 2
	
	mha := NewMultiHeadAttention(numHeads, dModel, 0.1)
	
	query := utils.RandomNormal([]int{batchSize, seqLen, dModel}, 0, 1)
	key := utils.RandomNormal([]int{batchSize, seqLen, dModel}, 0, 1)
	value := utils.RandomNormal([]int{batchSize, seqLen, dModel}, 0, 1)
	
	output := mha.Forward(query, key, value, nil, false)
	
	if output.Shape[0] != batchSize || output.Shape[1] != seqLen || output.Shape[2] != dModel {
		t.Errorf("Expected output shape [%d, %d, %d], got %v", 
			batchSize, seqLen, dModel, output.Shape)
	}
}

func TestTransformerBlock(t *testing.T) {
	dModel := 128
	numHeads := 4
	dFF := 512
	seqLen := 10
	batchSize := 2
	
	block := NewTransformerBlock(dModel, numHeads, dFF, 0.1)
	
	x := utils.RandomNormal([]int{batchSize, seqLen, dModel}, 0, 1)
	
	output := block.Forward(x, nil, false)
	
	if output.Shape[0] != batchSize || output.Shape[1] != seqLen || output.Shape[2] != dModel {
		t.Errorf("Expected output shape [%d, %d, %d], got %v",
			batchSize, seqLen, dModel, output.Shape)
	}
}

func TestSentenceEncoder(t *testing.T) {
	encoder := NewSentenceEncoder(384, MeanPooling)
	
	t.Run("SingleSentenceEncoding", func(t *testing.T) {
		text := "This is a test sentence for encoding."
		embedding := encoder.Encode(text)
		
		if embedding.Shape[0] != 1 || embedding.Shape[1] != 384 {
			t.Errorf("Expected shape [1, 384], got %v", embedding.Shape)
		}
		
		norm := 0.0
		for _, val := range embedding.Data {
			norm += val * val
		}
		norm = math.Sqrt(norm)
		
		if math.Abs(norm-1.0) > 0.01 {
			t.Errorf("Expected normalized embedding (norm=1.0), got norm=%f", norm)
		}
	})
	
	t.Run("BatchEncoding", func(t *testing.T) {
		texts := []string{
			"First sentence.",
			"Second sentence.",
			"Third sentence.",
		}
		
		embeddings := encoder.EncodeBatch(texts)
		
		if embeddings.Shape[0] != 3 || embeddings.Shape[1] != 384 {
			t.Errorf("Expected shape [3, 384], got %v", embeddings.Shape)
		}
	})
	
	t.Run("CosineSimilarity", func(t *testing.T) {
		// Test that cosine similarity is computed correctly (bounded between -1 and 1)
		// Note: With an untrained model, we cannot expect semantic similarity
		text1 := "The cat sits on the mat."
		text2 := "The cat is on the mat."
		
		emb1 := encoder.Encode(text1)
		emb2 := encoder.Encode(text2)
		
		sim := encoder.CosineSimilarity(emb1, emb2)
		
		// Cosine similarity should be between -1 and 1
		if sim < -1.0 || sim > 1.0 {
			t.Errorf("Cosine similarity out of bounds: %f", sim)
		}
		
		// Self-similarity should be 1.0 (or very close due to floating point)
		selfSim := encoder.CosineSimilarity(emb1, emb1)
		if math.Abs(selfSim - 1.0) > 1e-6 {
			t.Errorf("Self-similarity should be 1.0, got %f", selfSim)
		}
	})
}

func TestDocumentEncoder(t *testing.T) {
	encoder := NewDocumentEncoder(384, FixedSizeChunking)
	encoder.SetMaxChunkSize(10)
	
	t.Run("FixedSizeChunking", func(t *testing.T) {
		text := strings.Repeat("word ", 50)
		chunks := encoder.GetChunks(text)
		
		if len(chunks) == 0 {
			t.Error("Expected at least one chunk")
		}
		
		for i, chunk := range chunks {
			words := strings.Fields(chunk)
			if len(words) > 10 {
				t.Errorf("Chunk %d has %d words, expected <= 10", i, len(words))
			}
		}
	})
	
	t.Run("DocumentEncoding", func(t *testing.T) {
		document := "This is a long document. " + strings.Repeat("It contains multiple sentences. ", 20)
		embedding := encoder.EncodeDocument(document)
		
		if embedding.Shape[0] != 1 || embedding.Shape[1] != 384 {
			t.Errorf("Expected shape [1, 384], got %v", embedding.Shape)
		}
		
		norm := 0.0
		for _, val := range embedding.Data {
			norm += val * val
		}
		norm = math.Sqrt(norm)
		
		if math.Abs(norm-1.0) > 0.01 {
			t.Errorf("Expected normalized embedding (norm=1.0), got norm=%f", norm)
		}
	})
	
	t.Run("SlidingWindowChunking", func(t *testing.T) {
		encoder.chunkingStrategy = SlidingWindowChunking
		encoder.SetChunkOverlap(5)
		
		text := strings.Repeat("word ", 50)
		chunks := encoder.GetChunks(text)
		
		if len(chunks) < 2 {
			t.Error("Expected multiple overlapping chunks")
		}
	})
}

func TestPgVectorFormatter(t *testing.T) {
	formatter := NewPgVectorFormatter(384)
	
	t.Run("ToPgVector", func(t *testing.T) {
		embedding := utils.NewTensor(384)
		for i := range embedding.Data {
			embedding.Data[i] = float64(i) / 1000.0
		}
		
		pgvector := formatter.ToPgVector(embedding)
		
		if !strings.HasPrefix(pgvector, "[") || !strings.HasSuffix(pgvector, "]") {
			t.Errorf("Invalid pgvector format: %s", pgvector)
		}
		
		parts := strings.Split(pgvector[1:len(pgvector)-1], ",")
		if len(parts) != 384 {
			t.Errorf("Expected 384 dimensions, got %d", len(parts))
		}
	})
	
	t.Run("FromPgVector", func(t *testing.T) {
		pgvector := "[0.1,0.2,0.3"
		for i := 3; i < 384; i++ {
			pgvector += ",0.0"
		}
		pgvector += "]"
		
		tensor, err := formatter.FromPgVector(pgvector)
		if err != nil {
			t.Fatalf("Failed to parse pgvector: %v", err)
		}
		
		if len(tensor.Data) != 384 {
			t.Errorf("Expected 384 dimensions, got %d", len(tensor.Data))
		}
		
		if math.Abs(tensor.Data[0]-0.1) > 1e-6 {
			t.Errorf("Expected first element to be 0.1, got %f", tensor.Data[0])
		}
	})
	
	t.Run("SQLGeneration", func(t *testing.T) {
		createSQL := formatter.CreateTableSQL("embeddings")
		if !strings.Contains(createSQL, "vector(384)") {
			t.Error("Create table SQL should specify vector(384)")
		}
		
		indexSQL := formatter.CreateIndexSQL("embeddings", "embedding", "idx", "ivfflat")
		if !strings.Contains(indexSQL, "ivfflat") {
			t.Error("Index SQL should use ivfflat")
		}
		
		searchSQL := formatter.SimilaritySearchSQL("embeddings", 10)
		if !strings.Contains(searchSQL, "LIMIT 10") {
			t.Error("Search SQL should limit results")
		}
	})
}

func TestContrastiveLoss(t *testing.T) {
	loss := NewContrastiveLoss(0.07)
	
	embeddings := utils.RandomNormal([]int{4, 128}, 0, 1)
	labels := utils.NewTensorFromData([]float64{0, 0, 1, 1}, 4)
	
	lossVal, gradients := loss.ComputeLoss(embeddings, labels)
	
	if lossVal.Data[0] < 0 {
		t.Errorf("Loss should be non-negative, got %f", lossVal.Data[0])
	}
	
	if gradients.Shape[0] != 4 || gradients.Shape[1] != 128 {
		t.Errorf("Expected gradient shape [4, 128], got %v", gradients.Shape)
	}
}

func TestTripletLoss(t *testing.T) {
	loss := NewTripletLoss(0.5)
	
	anchors := utils.RandomNormal([]int{4, 128}, 0, 1)
	positives := utils.RandomNormal([]int{4, 128}, 0, 0.1)
	negatives := utils.RandomNormal([]int{4, 128}, 1, 1)
	
	lossVal, gradients := loss.ComputeLoss(anchors, positives, negatives)
	
	if lossVal.Data[0] < 0 {
		t.Errorf("Loss should be non-negative, got %f", lossVal.Data[0])
	}
	
	if gradients.Shape[0] != 4 || gradients.Shape[1] != 128 {
		t.Errorf("Expected gradient shape [4, 128], got %v", gradients.Shape)
	}
}

func TestPositionalEncoding(t *testing.T) {
	pe := NewPositionalEncoding(128, 100)
	
	input := utils.RandomNormal([]int{2, 50, 128}, 0, 1)
	output := pe.Forward(input)
	
	if output.Shape[0] != 2 || output.Shape[1] != 50 || output.Shape[2] != 128 {
		t.Errorf("Expected output shape [2, 50, 128], got %v", output.Shape)
	}
	
	// Check that at least some values are modified
	numChanged := 0
	for i := range input.Data {
		if output.Data[i] != input.Data[i] {
			numChanged++
		}
	}
	
	// At least 90% of values should be changed (allowing for some zeros in positional encoding)
	minChanged := int(float64(len(input.Data)) * 0.9)
	if numChanged < minChanged {
		t.Errorf("Positional encoding should modify most of the input. Only %d/%d values changed", 
			numChanged, len(input.Data))
	}
}

func TestLayerNorm(t *testing.T) {
	ln := NewLayerNorm(128)
	
	input := utils.RandomNormal([]int{2, 10, 128}, 0, 1)
	output := ln.Forward(input)
	
	if output.Shape[0] != 2 || output.Shape[1] != 10 || output.Shape[2] != 128 {
		t.Errorf("Expected output shape [2, 10, 128], got %v", output.Shape)
	}
}

func BenchmarkSentenceEncoding(b *testing.B) {
	encoder := NewSentenceEncoder(384, MeanPooling)
	text := "This is a benchmark test sentence for measuring encoding performance."
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = encoder.Encode(text)
	}
}

func BenchmarkBatchEncoding(b *testing.B) {
	encoder := NewSentenceEncoder(384, MeanPooling)
	texts := []string{
		"First sentence for batch encoding.",
		"Second sentence for batch encoding.",
		"Third sentence for batch encoding.",
		"Fourth sentence for batch encoding.",
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = encoder.EncodeBatch(texts)
	}
}

func BenchmarkDocumentEncoding(b *testing.B) {
	encoder := NewDocumentEncoder(384, FixedSizeChunking)
	document := strings.Repeat("This is a test sentence. ", 100)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = encoder.EncodeDocument(document)
	}
}