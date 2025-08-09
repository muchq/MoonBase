# Model Management Best Practices

## Where Models Come From

Models are created by training neural networks on data:

```bash
# Train a model (creates models/mnist_vendored_model/)
bazel run //go/neuro/examples:mnist_vendored

# Files created:
models/mnist_vendored_model/
├── model_config.json    # Architecture definition
└── model_weights.json   # Trained parameters
```

## Model Storage Options

### Development
- **Local filesystem**: Store in `models/` directory (gitignored)
- **Purpose**: Quick iteration and testing
- **Lifetime**: Temporary, deleted on clean

### Production
Models should be stored in appropriate external storage:

1. **Object Storage** (Recommended)
   - AWS S3, Google Cloud Storage, Azure Blob Storage
   - Versioned with timestamps or git commits
   - Example: `s3://my-models/mnist/v1.2.3/`

2. **Model Registry**
   - MLflow, Kubeflow, SageMaker Model Registry
   - Includes metadata, metrics, lineage
   - Example: `mlflow://models/mnist/production`

3. **Container Images**
   - Bake model into Docker image for immutable deployments
   - Example: `myapp:v1.2.3` with model at `/app/model/`

4. **Git LFS** (Small models only)
   - For models under 100MB
   - Track with Git Large File Storage
   - Keep in separate repository from code

## Production Deployment Pattern

```go
// Bad: Hardcoded local path
engine, _ := inference.LoadModelForInference("models/my_model")

// Good: Configurable model location
modelPath := os.Getenv("MODEL_PATH")  // s3://bucket/model.json
engine, _ := inference.LoadModelFromURL(modelPath)
```

## Model Versioning

Always version your models:

```bash
models/
├── mnist/
│   ├── v1.0.0/
│   │   ├── model_config.json
│   │   └── model_weights.json
│   ├── v1.1.0/
│   │   ├── model_config.json
│   │   └── model_weights.json
│   └── latest -> v1.1.0
```

## CI/CD Pipeline

1. **Training Pipeline**
   ```yaml
   steps:
     - train_model
     - evaluate_model
     - if: metrics.accuracy > threshold
       then: upload_to_s3
   ```

2. **Deployment Pipeline**
   ```yaml
   steps:
     - download_model_from_s3
     - build_container_with_model
     - deploy_to_kubernetes
   ```

## Security Considerations

1. **Don't commit models to Git** (except tiny demo models)
2. **Use signed URLs** for S3/GCS access
3. **Encrypt models at rest** in storage
4. **Audit model access** in production
5. **Validate model checksums** before loading

## Example Production Setup

```go
// production_config.go
type ModelConfig struct {
    Source    string `env:"MODEL_SOURCE"`     // s3://models/mnist/v1.2.3
    CacheDir  string `env:"MODEL_CACHE_DIR"`  // /var/cache/models
    MaxAge    time.Duration                    // 24h
    Checksum  string `env:"MODEL_CHECKSUM"`   // sha256:abc123...
}

// production_loader.go
func LoadProductionModel(config ModelConfig) (*inference.Engine, error) {
    // Check cache first
    if cached := checkCache(config.CacheDir); cached != nil {
        return cached, nil
    }
    
    // Download from S3/GCS
    model := downloadModel(config.Source)
    
    // Verify checksum
    if !verifyChecksum(model, config.Checksum) {
        return nil, errors.New("model checksum mismatch")
    }
    
    // Cache locally
    saveToCache(model, config.CacheDir)
    
    return inference.LoadModel(model)
}
```

## Monitoring

Track these metrics in production:
- Model version in use
- Inference latency by model version
- Model load time
- Cache hit rate
- Model file size

## Summary

- **Development**: Local `models/` directory (gitignored)
- **Production**: External storage (S3, GCS, registry)
- **Never**: Commit models to Git (except tiny demos)
- **Always**: Version models properly
- **Consider**: Model size, update frequency, deployment method