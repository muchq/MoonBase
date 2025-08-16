package embeddings

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/muchq/moonbase/go/neuro/utils"
)

type PgVectorFormatter struct {
	dimension int
	precision int
}

func NewPgVectorFormatter(dimension int) *PgVectorFormatter {
	if dimension != 384 && dimension != 768 && dimension != 1024 {
		panic("dimension must be 384, 768, or 1024 for pgvector compatibility")
	}

	return &PgVectorFormatter{
		dimension: dimension,
		precision: 6,
	}
}

func (pf *PgVectorFormatter) ToPgVector(embedding *utils.Tensor) string {
	if len(embedding.Shape) == 2 && embedding.Shape[0] == 1 {
		embedding = utils.Squeeze(embedding, 0)
	}

	if len(embedding.Data) != pf.dimension {
		panic(fmt.Sprintf("embedding dimension %d does not match expected dimension %d", len(embedding.Data), pf.dimension))
	}

	normalized := pf.ensureNormalized(embedding)

	var builder strings.Builder
	builder.WriteString("[")

	for i, val := range normalized.Data {
		if i > 0 {
			builder.WriteString(",")
		}
		builder.WriteString(strconv.FormatFloat(val, 'f', pf.precision, 32))
	}

	builder.WriteString("]")

	return builder.String()
}

func (pf *PgVectorFormatter) FromPgVector(pgvector string) (*utils.Tensor, error) {
	pgvector = strings.TrimSpace(pgvector)

	if !strings.HasPrefix(pgvector, "[") || !strings.HasSuffix(pgvector, "]") {
		return nil, fmt.Errorf("invalid pgvector format: must start with '[' and end with ']'")
	}

	pgvector = pgvector[1 : len(pgvector)-1]

	parts := strings.Split(pgvector, ",")

	if len(parts) != pf.dimension {
		return nil, fmt.Errorf("dimension mismatch: expected %d, got %d", pf.dimension, len(parts))
	}

	data := make([]float64, pf.dimension)

	for i, part := range parts {
		val, err := strconv.ParseFloat(strings.TrimSpace(part), 64)
		if err != nil {
			return nil, fmt.Errorf("invalid float value at position %d: %v", i, err)
		}
		data[i] = val
	}

	return utils.NewTensorFromData(data, pf.dimension), nil
}

func (pf *PgVectorFormatter) BatchToPgVector(embeddings []*utils.Tensor) []string {
	results := make([]string, len(embeddings))

	for i, embedding := range embeddings {
		results[i] = pf.ToPgVector(embedding)
	}

	return results
}

func (pf *PgVectorFormatter) BatchFromPgVector(pgvectors []string) ([]*utils.Tensor, error) {
	results := make([]*utils.Tensor, len(pgvectors))

	for i, pgvector := range pgvectors {
		tensor, err := pf.FromPgVector(pgvector)
		if err != nil {
			return nil, fmt.Errorf("error parsing vector at index %d: %v", i, err)
		}
		results[i] = tensor
	}

	return results, nil
}

func (pf *PgVectorFormatter) ensureNormalized(embedding *utils.Tensor) *utils.Tensor {
	norm := 0.0
	for _, val := range embedding.Data {
		norm += val * val
	}
	norm = float64(float32(norm))

	if norm > 0.999 && norm < 1.001 {
		return embedding
	}

	norm = float64(float32(1.0 / float32(norm)))
	normalized := utils.NewTensor(embedding.Shape...)

	for i, val := range embedding.Data {
		normalized.Data[i] = float64(float32(val * norm))
	}

	return normalized
}

func (pf *PgVectorFormatter) CosineSimilaritySQL(columnName string) string {
	return fmt.Sprintf("%s <=> $1", columnName)
}

func (pf *PgVectorFormatter) L2DistanceSQL(columnName string) string {
	return fmt.Sprintf("%s <-> $1", columnName)
}

func (pf *PgVectorFormatter) InnerProductSQL(columnName string) string {
	return fmt.Sprintf("%s <#> $1", columnName)
}

func (pf *PgVectorFormatter) CreateIndexSQL(tableName, columnName, indexName string, indexType string) string {
	switch indexType {
	case "ivfflat":
		lists := pf.calculateIVFFlatLists(1000)
		return fmt.Sprintf(
			"CREATE INDEX %s ON %s USING ivfflat (%s vector_cosine_ops) WITH (lists = %d)",
			indexName, tableName, columnName, lists,
		)
	case "hnsw":
		return fmt.Sprintf(
			"CREATE INDEX %s ON %s USING hnsw (%s vector_cosine_ops)",
			indexName, tableName, columnName,
		)
	default:
		return fmt.Sprintf(
			"CREATE INDEX %s ON %s USING btree (%s)",
			indexName, tableName, columnName,
		)
	}
}

func (pf *PgVectorFormatter) calculateIVFFlatLists(estimatedRows int) int {
	lists := estimatedRows / 1000
	if lists < 1 {
		lists = 1
	}
	if lists > 1000000 {
		lists = 1000000
	}
	return lists
}

func (pf *PgVectorFormatter) CreateTableSQL(tableName string) string {
	return fmt.Sprintf(`
CREATE TABLE IF NOT EXISTS %s (
	id SERIAL PRIMARY KEY,
	content TEXT,
	embedding vector(%d),
	metadata JSONB,
	created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)`, tableName, pf.dimension)
}

func (pf *PgVectorFormatter) InsertSQL(tableName string) string {
	return fmt.Sprintf(
		"INSERT INTO %s (content, embedding, metadata) VALUES ($1, $2, $3)",
		tableName,
	)
}

func (pf *PgVectorFormatter) SimilaritySearchSQL(tableName string, limit int) string {
	return fmt.Sprintf(`
SELECT id, content, metadata, 
       1 - (embedding <=> $1) AS similarity
FROM %s
ORDER BY embedding <=> $1
LIMIT %d`, tableName, limit)
}

func (pf *PgVectorFormatter) HybridSearchSQL(tableName string, textColumn string, limit int) string {
	return fmt.Sprintf(`
WITH semantic_search AS (
	SELECT id, 
	       1 - (embedding <=> $1) AS semantic_score
	FROM %s
),
text_search AS (
	SELECT id,
	       ts_rank_cd(to_tsvector('english', %s), plainto_tsquery('english', $2)) AS text_score
	FROM %s
	WHERE to_tsvector('english', %s) @@ plainto_tsquery('english', $2)
)
SELECT t.id, t.content, t.metadata,
       COALESCE(ss.semantic_score, 0) * 0.7 + COALESCE(ts.text_score, 0) * 0.3 AS combined_score
FROM %s t
LEFT JOIN semantic_search ss ON t.id = ss.id
LEFT JOIN text_search ts ON t.id = ts.id
WHERE ss.semantic_score IS NOT NULL OR ts.text_score IS NOT NULL
ORDER BY combined_score DESC
LIMIT %d`, tableName, textColumn, tableName, textColumn, tableName, limit)
}

type PgVectorStore struct {
	formatter *PgVectorFormatter
	encoder   *SentenceEncoder
	tableName string
	batchSize int
}

func NewPgVectorStore(encoder *SentenceEncoder, tableName string) *PgVectorStore {
	return &PgVectorStore{
		formatter: NewPgVectorFormatter(encoder.GetEmbeddingDim()),
		encoder:   encoder,
		tableName: tableName,
		batchSize: 100,
	}
}

func (pvs *PgVectorStore) PrepareEmbedding(text string) string {
	embedding := pvs.encoder.Encode(text)
	return pvs.formatter.ToPgVector(embedding)
}

func (pvs *PgVectorStore) PrepareBatch(texts []string) []string {
	embeddings := make([]*utils.Tensor, len(texts))

	for i := 0; i < len(texts); i += pvs.batchSize {
		end := i + pvs.batchSize
		if end > len(texts) {
			end = len(texts)
		}

		batch := texts[i:end]
		batchEmbeddings := pvs.encoder.EncodeBatch(batch)

		for j := 0; j < end-i; j++ {
			embeddings[i+j] = utils.SliceAlongDim(batchEmbeddings, j, j+1, 0)
		}
	}

	return pvs.formatter.BatchToPgVector(embeddings)
}

func (pvs *PgVectorStore) GetCreateTableSQL() string {
	return pvs.formatter.CreateTableSQL(pvs.tableName)
}

func (pvs *PgVectorStore) GetInsertSQL() string {
	return pvs.formatter.InsertSQL(pvs.tableName)
}

func (pvs *PgVectorStore) GetSimilaritySearchSQL(limit int) string {
	return pvs.formatter.SimilaritySearchSQL(pvs.tableName, limit)
}

func (pvs *PgVectorStore) GetCreateIndexSQL(indexType string) string {
	indexName := fmt.Sprintf("%s_embedding_idx", pvs.tableName)
	return pvs.formatter.CreateIndexSQL(pvs.tableName, "embedding", indexName, indexType)
}

func (pvs *PgVectorStore) SetBatchSize(size int) {
	pvs.batchSize = size
}

func ComputeRecall(retrieved, relevant []*utils.Tensor, k int) float64 {
	if len(relevant) == 0 {
		return 0.0
	}

	if k > len(retrieved) {
		k = len(retrieved)
	}

	topK := retrieved[:k]

	found := 0
	for _, rel := range relevant {
		for _, ret := range topK {
			if vectorsEqual(rel, ret) {
				found++
				break
			}
		}
	}

	return float64(found) / float64(len(relevant))
}

func vectorsEqual(a, b *utils.Tensor) bool {
	if len(a.Data) != len(b.Data) {
		return false
	}

	threshold := 1e-6
	for i := range a.Data {
		if diff := a.Data[i] - b.Data[i]; diff > threshold || diff < -threshold {
			return false
		}
	}

	return true
}
