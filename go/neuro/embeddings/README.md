# Neuro Embeddings Module

This module provides a complete framework for training, generating, and using text embeddings with support for various training objectives and PostgreSQL pgvector integration.

## Overview

The embeddings module includes:
- Sentence encoder with transformer architecture
- Multiple training objectives (contrastive, triplet, STS, MLM)
- Pre-trained model support
- pgvector database integration
- Document chunking and encoding

## Data Requirements for Training

### 1. Contrastive Learning (`ContrastiveLoss`)
- **Data Format**: Pairs/batches of text with labels indicating similarity
- **Use Case**: Group similar texts together
- **Example**: Customer support tickets grouped by topic, product reviews by sentiment
```go
// Example data structure
texts := []string{"text1", "text2", "text3", ...}
labels := []float64{0, 0, 1, ...} // Same label = similar texts
```

### 2. Triplet Learning (`TripletLoss`)
- **Data Format**: Triplets of (anchor, positive, negative) texts
  - Anchor: Reference text
  - Positive: Similar/related text
  - Negative: Dissimilar/unrelated text
- **Example**: (query, relevant_doc, irrelevant_doc)
```go
anchors := []string{"What is machine learning?", ...}
positives := []string{"ML is a subset of AI...", ...}
negatives := []string{"The weather today is...", ...}
```

### 3. Semantic Textual Similarity (`SemanticTextualSimilarity`)
- **Data Format**: Pairs of texts with similarity scores (0-5 scale)
- **Use Case**: Fine-tuning on STS benchmarks
- **Example**: STS-B dataset with sentence pairs and human ratings
```go
text1 := []string{"A man is eating food.", ...}
text2 := []string{"A person is consuming a meal.", ...}
scores := []float64{4.5, ...} // 0-5 similarity score
```

### 4. Masked Language Modeling (`MaskedLanguageModel`)
- **Data Format**: Raw text corpus for self-supervised pre-training
- **Use Case**: Learn general language representations
- **Example**: Wikipedia, books, web crawl data
```go
corpus := []string{
    "The quick brown fox jumps over the lazy dog.",
    "Machine learning is transforming industries.",
    ...
}
```

## Training a Model

### Basic Training Setup

```go
package main

import (
    "github.com/muchq/moonbase/go/neuro/embeddings"
    "github.com/muchq/moonbase/go/neuro/data"
    "github.com/muchq/moonbase/go/neuro/utils"
)

func trainModel() {
    // Initialize model
    embeddingDim := 384
    encoder := embeddings.NewSentenceEncoder(embeddingDim, embeddings.MeanPooling)
    
    // Create trainer
    learningRate := 0.001
    trainer := embeddings.NewEmbeddingTrainer(encoder, learningRate)
    
    // Add training objectives
    contrastiveLoss := embeddings.NewContrastiveLoss(0.07) // temperature
    trainer.AddObjective(contrastiveLoss)
    
    // Prepare data (example with contrastive learning)
    texts := []string{
        "The cat sits on the mat.",
        "A feline rests on the rug.",
        "The weather is sunny today.",
        "It's a bright day outside.",
    }
    labels := []float64{0, 0, 1, 1} // Group similar sentences
    
    // Convert texts to tensors (simplified - actual implementation needs tokenization)
    // This is pseudocode - you'll need to tokenize and convert to input IDs
    xData := tokenizeAndEncode(texts, encoder)
    yData := utils.NewTensorFromData(labels, len(labels))
    
    // Create dataset
    batchSize := 32
    dataset := data.NewDataset(xData, yData, batchSize, true)
    
    // Training loop
    epochs := 10
    for epoch := 0; epoch < epochs; epoch++ {
        dataset.Reset()
        totalLoss := 0.0
        
        for dataset.HasNext() {
            xBatch, yBatch := dataset.NextBatch()
            
            // Forward pass
            embeddings := encoder.EncodeBatch(xBatch)
            
            // Compute loss and gradients
            loss, gradients := contrastiveLoss.ComputeLoss(embeddings, yBatch)
            
            // Backward pass and update weights
            trainer.optimizer.Update(encoder.GetParameters(), gradients)
            
            totalLoss += loss.Data[0]
        }
        
        fmt.Printf("Epoch %d, Loss: %.4f\n", epoch+1, totalLoss/float64(dataset.NumBatches()))
    }
}
```

### Training with Triplet Loss

```go
func trainWithTripletLoss() {
    encoder := embeddings.NewSentenceEncoder(384, embeddings.MeanPooling)
    trainer := embeddings.NewEmbeddingTrainer(encoder, 0.001)
    
    // Add triplet loss objective
    margin := 0.5
    tripletLoss := embeddings.NewTripletLoss(margin)
    trainer.AddObjective(tripletLoss)
    
    // Prepare triplet data
    anchors := prepareAnchors()    // Your anchor texts
    positives := preparePositives() // Similar to anchors
    negatives := prepareNegatives() // Different from anchors
    
    // Training loop with triplets
    for epoch := 0; epoch < epochs; epoch++ {
        anchorEmb := encoder.EncodeBatch(anchors)
        positiveEmb := encoder.EncodeBatch(positives)
        negativeEmb := encoder.EncodeBatch(negatives)
        
        loss, grad := tripletLoss.ComputeLoss(anchorEmb, positiveEmb, negativeEmb)
        trainer.optimizer.Update(encoder.GetParameters(), grad)
    }
}
```

## Saving and Loading Models

### Save a Trained Model

```go
func saveModel(encoder *embeddings.SentenceEncoder, path string) error {
    // Create pre-trained model structure
    model := &embeddings.PretrainedModel{
        Name:         "my-sentence-encoder",
        Architecture: "transformer",
        EmbeddingDim: encoder.GetEmbeddingDim(),
        VocabSize:    encoder.tokenizer.VocabSize(),
        NumLayers:    len(encoder.transformerBlocks),
        NumHeads:     8, // Configure based on your model
        HiddenDim:    encoder.GetEmbeddingDim(),
        MaxSeqLength: 512,
        Weights:      encoder.GetWeights(), // Extract model weights
    }
    
    // Use custom model loader to save
    loader := embeddings.NewCustomModelLoader()
    return loader.Save(model, path)
}
```

### Load a Pre-trained Model

```go
func loadModel(path string) (*embeddings.SentenceEncoder, error) {
    loader := embeddings.NewCustomModelLoader()
    pretrainedModel, err := loader.Load(path)
    if err != nil {
        return nil, err
    }
    
    // Initialize encoder with loaded weights
    encoder := embeddings.NewSentenceEncoder(
        pretrainedModel.EmbeddingDim,
        embeddings.MeanPooling,
    )
    encoder.LoadWeights(pretrainedModel.Weights)
    
    return encoder, nil
}
```

## Creating and Using Embeddings

### Generate Embeddings for Single Text

```go
func generateEmbedding(encoder *embeddings.SentenceEncoder, text string) *utils.Tensor {
    // Generate embedding for a single text
    embedding := encoder.Encode(text)
    return embedding
}
```

### Generate Embeddings for Multiple Texts

```go
func generateBatchEmbeddings(encoder *embeddings.SentenceEncoder, texts []string) *utils.Tensor {
    // Generate embeddings for multiple texts
    embeddings := encoder.EncodeBatch(texts)
    return embeddings
}
```

### Compute Similarity Between Texts

```go
func computeSimilarity(encoder *embeddings.SentenceEncoder, text1, text2 string) float64 {
    emb1 := encoder.Encode(text1)
    emb2 := encoder.Encode(text2)
    
    // Cosine similarity (embeddings are L2-normalized)
    similarity := encoder.CosineSimilarity(emb1, emb2)
    return similarity
}
```

### Find Similar Texts

```go
func findSimilarTexts(encoder *embeddings.SentenceEncoder, query string, corpus []string, topK int) []int {
    queryEmb := encoder.Encode(query)
    
    type scoreIndex struct {
        score float64
        index int
    }
    
    scores := make([]scoreIndex, len(corpus))
    for i, text := range corpus {
        emb := encoder.Encode(text)
        score := encoder.CosineSimilarity(queryEmb, emb)
        scores[i] = scoreIndex{score, i}
    }
    
    // Sort by similarity score
    sort.Slice(scores, func(i, j int) bool {
        return scores[i].score > scores[j].score
    })
    
    // Return top K indices
    results := make([]int, topK)
    for i := 0; i < topK && i < len(scores); i++ {
        results[i] = scores[i].index
    }
    return results
}
```

## pgvector Integration

The module includes full support for storing and querying embeddings in PostgreSQL with the pgvector extension.

### Setup pgvector Database

```sql
-- Enable pgvector extension
CREATE EXTENSION IF NOT EXISTS vector;

-- Create table for embeddings
CREATE TABLE documents (
    id SERIAL PRIMARY KEY,
    content TEXT,
    embedding vector(384), -- Match your embedding dimension
    metadata JSONB
);

-- Create index for similarity search
CREATE INDEX ON documents USING ivfflat (embedding vector_cosine_ops) WITH (lists = 100);
```

### Store Embeddings in pgvector

```go
import (
    "database/sql"
    "github.com/muchq/moonbase/go/neuro/embeddings"
    _ "github.com/lib/pq"
)

func storeEmbeddings(encoder *embeddings.SentenceEncoder, db *sql.DB) error {
    formatter := embeddings.NewPgVectorFormatter(encoder.GetEmbeddingDim())
    
    texts := []string{
        "First document content",
        "Second document content",
        // ...
    }
    
    for _, text := range texts {
        // Generate embedding
        embedding := encoder.Encode(text)
        
        // Format for pgvector
        vectorStr := formatter.FormatEmbedding(embedding)
        
        // Insert into database
        query := `
            INSERT INTO documents (content, embedding) 
            VALUES ($1, $2::vector)
        `
        _, err := db.Exec(query, text, vectorStr)
        if err != nil {
            return err
        }
    }
    
    return nil
}
```

### Query Similar Documents

```go
func findSimilarDocuments(encoder *embeddings.SentenceEncoder, db *sql.DB, query string, limit int) ([]string, error) {
    formatter := embeddings.NewPgVectorFormatter(encoder.GetEmbeddingDim())
    
    // Generate query embedding
    queryEmb := encoder.Encode(query)
    vectorStr := formatter.FormatEmbedding(queryEmb)
    
    // Query similar documents using cosine similarity
    sqlQuery := fmt.Sprintf(`
        SELECT content, 1 - (embedding <=> $1::vector) as similarity
        FROM documents
        ORDER BY embedding <=> $1::vector
        LIMIT $2
    `)
    
    rows, err := db.Query(sqlQuery, vectorStr, limit)
    if err != nil {
        return nil, err
    }
    defer rows.Close()
    
    var results []string
    for rows.Next() {
        var content string
        var similarity float64
        if err := rows.Scan(&content, &similarity); err != nil {
            return nil, err
        }
        results = append(results, content)
        fmt.Printf("Similarity: %.4f - %s\n", similarity, content)
    }
    
    return results, nil
}
```

### Batch Processing for Large Datasets

```go
func batchProcessDocuments(encoder *embeddings.SentenceEncoder, db *sql.DB, documents []string) error {
    formatter := embeddings.NewPgVectorFormatter(encoder.GetEmbeddingDim())
    
    batchSize := 100
    for i := 0; i < len(documents); i += batchSize {
        end := i + batchSize
        if end > len(documents) {
            end = len(documents)
        }
        
        batch := documents[i:end]
        embeddings := encoder.EncodeBatch(batch)
        
        // Use COPY for efficient bulk insert
        stmt, err := db.Prepare(pq.CopyIn("documents", "content", "embedding"))
        if err != nil {
            return err
        }
        
        for j, doc := range batch {
            embSlice := embeddings.GetRow(j)
            embTensor := utils.NewTensorFromData(embSlice, len(embSlice))
            vectorStr := formatter.FormatEmbedding(embTensor)
            
            _, err = stmt.Exec(doc, vectorStr)
            if err != nil {
                return err
            }
        }
        
        _, err = stmt.Exec()
        if err != nil {
            return err
        }
        stmt.Close()
    }
    
    return nil
}
```

### Advanced pgvector Queries

```go
func advancedSimilaritySearch(encoder *embeddings.SentenceEncoder, db *sql.DB, query string, filters map[string]interface{}) ([]Document, error) {
    formatter := embeddings.NewPgVectorFormatter(encoder.GetEmbeddingDim())
    
    queryEmb := encoder.Encode(query)
    vectorStr := formatter.FormatEmbedding(queryEmb)
    
    // Build query with metadata filters
    sqlQuery := `
        SELECT id, content, metadata,
               1 - (embedding <=> $1::vector) as similarity
        FROM documents
        WHERE metadata @> $2
          AND 1 - (embedding <=> $1::vector) > 0.7  -- Similarity threshold
        ORDER BY embedding <=> $1::vector
        LIMIT 10
    `
    
    filterJSON, _ := json.Marshal(filters)
    rows, err := db.Query(sqlQuery, vectorStr, filterJSON)
    // ... process results
}
```

## Document Processing

The module includes a document encoder for handling longer texts:

```go
func processLongDocument(text string) {
    // Create document encoder with chunking strategy
    encoder := embeddings.NewDocumentEncoder(384, embeddings.FixedSizeChunking)
    encoder.SetMaxChunkSize(512) // tokens per chunk
    
    // Process document
    chunks := encoder.ChunkDocument(text)
    embeddings := encoder.EncodeDocument(text)
    
    // Store each chunk with its embedding
    for i, chunk := range chunks {
        chunkEmbedding := embeddings.GetRow(i)
        // Store in database...
    }
}
```

## Performance Optimization

### Hardware Acceleration
- The module supports batch processing for efficient GPU utilization
- Use larger batch sizes for better throughput

### Caching
```go
type EmbeddingCache struct {
    cache map[string]*utils.Tensor
    encoder *embeddings.SentenceEncoder
}

func (ec *EmbeddingCache) GetEmbedding(text string) *utils.Tensor {
    if emb, exists := ec.cache[text]; exists {
        return emb
    }
    emb := ec.encoder.Encode(text)
    ec.cache[text] = emb
    return emb
}
```

### Quantization for Storage
- Consider quantizing embeddings to int8 for 4x storage reduction
- pgvector supports halfvec type for 16-bit precision

## Best Practices

1. **Data Preparation**: Clean and normalize text before training
2. **Batch Size**: Use largest batch size that fits in memory
3. **Learning Rate**: Start with 1e-3 and adjust based on loss curves
4. **Evaluation**: Use validation set with semantic similarity tasks
5. **Model Selection**: Choose pooling strategy based on downstream task
   - Mean pooling: General purpose
   - Max pooling: Key feature extraction
   - CLS pooling: When using BERT-style pre-training

## Troubleshooting

### Common Issues

1. **Out of Memory**: Reduce batch size or embedding dimension
2. **Poor Similarity Scores**: Ensure model is trained on domain-specific data
3. **Slow Training**: Enable GPU support or use smaller model
4. **Database Performance**: Create appropriate indexes on vector columns

## Examples

See the `examples/` directory for complete working examples:
- `semantic_search.go`: Build a semantic search engine
- `document_clustering.go`: Cluster documents by similarity
- `train_custom_model.go`: Train on your own dataset