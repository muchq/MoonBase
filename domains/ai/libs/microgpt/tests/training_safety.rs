use microgpt::{
    DType, Device, ModelConfig, TensorAdam, TensorGpt, TrainConfig, tensor_train_step_batched,
};

#[test]
fn test_train_step_with_empty_sequences() {
    // Verify that the training step doesn't panic and returns an error
    // when sequences are too short to have targets (len < 2).
    let config = ModelConfig {
        n_embd: 8,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;
    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);

    let train_config = TrainConfig::default();
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();

    // Batch with only single-token sequences (no targets possible)
    let batch = vec![
        vec![1usize],
        vec![2usize],
    ];

    let result = tensor_train_step_batched(&model, &batch, &mut optimizer, &train_config, 0);
    assert!(result.is_err());
    assert!(result.unwrap_err().to_string().contains("no real tokens"));
}

#[test]
fn test_train_step_truncation() {
    // Verify that sequences longer than block_size are truncated correctly
    // and don't cause out-of-bounds or shape mismatches.
    let config = ModelConfig {
        n_embd: 8,
        n_head: 2,
        n_layer: 1,
        block_size: 4, // Very small block size
    };
    let vocab_size = 10;
    let device = Device::Cpu;
    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);

    let train_config = TrainConfig::default();
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();

    // Sequence longer than block_size (len=10, block_size=4)
    let batch = vec![
        vec![0usize, 1, 2, 3, 4, 5, 6, 7, 8, 9],
    ];

    let loss = tensor_train_step_batched(&model, &batch, &mut optimizer, &train_config, 0).unwrap();
    assert!(loss.is_finite());
}

#[test]
fn test_train_step_mixed_lengths() {
    // Verify that a batch with mixed lengths (some requiring truncation, some not)
    // works correctly.
    let config = ModelConfig {
        n_embd: 8,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;
    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);

    let train_config = TrainConfig::default();
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();

    let batch = vec![
        vec![0usize, 1, 2],             // Short
        vec![0usize, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10], // Long (needs truncation)
        vec![5usize, 6],                // Minimal
    ];

    let loss = tensor_train_step_batched(&model, &batch, &mut optimizer, &train_config, 0).unwrap();
    assert!(loss.is_finite());
}

#[test]
fn test_optimizer_detach_prevention_of_graph_growth() {
    // This is a proxy test. We can't easily measure the "graph size" in bytes here
    // without deep candle internals, but we can verify that repeated steps
    // don't cause an obvious explosion in memory/time in a small scale.
    // The real verification was the user's report of slow down, and our fix
    // was calling .detach() on the accumulators.
    
    let config = ModelConfig {
        n_embd: 128,
        n_head: 4,
        n_layer: 4,
        block_size: 128,
    };
    let vocab_size = 100;
    let device = Device::Cpu;
    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);

    let train_config = TrainConfig {
        learning_rate: 0.001,
        num_steps: 100,
        ..Default::default()
    };
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();

    let batch = vec![vec![0usize; 128]];

    // Run 50 steps and ensure they remain fast. If the graph were leaking,
    // it would slow down quadratically or worse.
    for i in 0..50 {
        let now = std::time::Instant::now();
        tensor_train_step_batched(&model, &batch, &mut optimizer, &train_config, i).unwrap();
        let elapsed = now.elapsed();
        // On modern CPUs, 128x128 4-layer GPT should be well under 1s per step.
        assert!(elapsed.as_secs_f64() < 2.0, "Step {i} took too long: {elapsed:?}");
    }
}
