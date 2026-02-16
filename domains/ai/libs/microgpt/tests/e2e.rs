use microgpt::{
    Device, InferenceGpt, ModelConfig, TensorAdam, TensorGpt, TrainConfig, tensor_train_step,
};
use microgpt::model::InferenceKvCache;

#[test]
fn test_forward_pass_parity() {
    // Verify that TensorGpt (batched) and InferenceGpt (autoregressive f64)
    // produce the same logits for position 0 given the same weights.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 42;
    let device = Device::Cpu;

    let tensor_gpt = TensorGpt::new(vocab_size, seed, config, &device);
    let json = tensor_gpt.save_weights();
    let inference_gpt = InferenceGpt::load_weights_with_config(vocab_size, &json, config).unwrap();

    let token_id = 1;

    // TensorGpt batched forward for a single token at position 0
    let logits_tensor = tensor_gpt.forward(&[token_id]).unwrap();
    let logits_tensor: Vec<f64> = logits_tensor.squeeze(0).unwrap().to_vec1().unwrap();

    // InferenceGpt autoregressive forward for the same token at position 0
    let mut kv_inf = InferenceKvCache::new(&config);
    let logits_inf = inference_gpt.forward(token_id, 0, &mut kv_inf);

    assert_eq!(logits_tensor.len(), logits_inf.len());
    for (i, (a, b)) in logits_tensor.iter().zip(logits_inf.iter()).enumerate() {
        assert!(
            (a - b).abs() < 1e-10,
            "mismatch at index {}: {} vs {}", i, a, b
        );
    }
}

#[test]
fn test_train_save_load_roundtrip() {
    // Train for N steps with TensorGpt, save weights, reload into
    // InferenceGpt, verify generation works and is deterministic.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 42;
    let device = Device::Cpu;

    let model = TensorGpt::new(vocab_size, seed, config, &device);

    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 5,
        ..Default::default()
    };

    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
    let tokens = vec![1, 2, 3, 4, 1, 2, 3, 4];

    for i in 0..train_config.num_steps {
        let _loss = tensor_train_step(&model, &tokens, &mut optimizer, &train_config, i).unwrap();
    }

    let json = model.save_weights();
    let inference_gpt = InferenceGpt::load_weights_with_config(vocab_size, &json, config).unwrap();

    let seed_gen = 123;
    let output1 = inference_gpt.generate(1, 1.0, seed_gen, |id| Some((id as u8 + b'a') as char));
    let output2 = inference_gpt.generate(1, 1.0, seed_gen, |id| Some((id as u8 + b'a') as char));

    assert_eq!(output1, output2);
    assert!(!output1.is_empty());
}

#[test]
fn test_weight_serialization_roundtrip() {
    // Save TensorGpt weights, reload them, confirm they match exactly.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 99;
    let device = Device::Cpu;

    let model = TensorGpt::new(vocab_size, seed, config, &device);
    let json = model.save_weights();

    let model2 = TensorGpt::load_weights_with_config(vocab_size, &json, config, &device).unwrap();
    let json2 = model2.save_weights();

    // Parse both JSON weight snapshots and compare values.
    let snap1: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&json).unwrap();
    let snap2: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&json2).unwrap();

    assert_eq!(snap1.len(), snap2.len());
    for (k, v1) in &snap1 {
        let v2 = snap2.get(k).expect("missing key in reloaded model");
        for (row1, row2) in v1.iter().zip(v2.iter()) {
            for (x1, x2) in row1.iter().zip(row2.iter()) {
                assert!(
                    (x1 - x2).abs() < 1e-15,
                    "weight mismatch for key {}: {} vs {}", k, x1, x2
                );
            }
        }
    }
}

#[test]
fn test_deterministic_training() {
    // Two training runs with the same seed produce identical weights.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 42;
    let tokens = vec![1, 2, 3, 4, 5, 1, 2, 3];
    let train_config = TrainConfig {
        learning_rate: 0.1,
        num_steps: 5,
        ..Default::default()
    };
    let device = Device::Cpu;

    let run_training = || {
        let model = TensorGpt::new(vocab_size, seed, config, &device);
        let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
        for i in 0..train_config.num_steps {
            tensor_train_step(&model, &tokens, &mut optimizer, &train_config, i).unwrap();
        }
        model.save_weights()
    };

    let weights1 = run_training();
    let weights2 = run_training();

    let snap1: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&weights1).unwrap();
    let snap2: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&weights2).unwrap();

    for (k, v1) in &snap1 {
        let v2 = snap2.get(k).expect("missing key in second run");
        for (row1, row2) in v1.iter().zip(v2.iter()) {
            for (x1, x2) in row1.iter().zip(row2.iter()) {
                assert_eq!(x1, x2, "Weight mismatch for key {}", k);
            }
        }
    }
}
