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
    let logits_tensor: Vec<f32> = logits_tensor.squeeze(0).unwrap().to_vec1().unwrap();

    // InferenceGpt autoregressive forward for the same token at position 0
    let mut kv_inf = InferenceKvCache::new(&config);
    let logits_inf = inference_gpt.forward(token_id, 0, &mut kv_inf);

    assert_eq!(logits_tensor.len(), logits_inf.len());
    for (i, (a, b)) in logits_tensor.iter().zip(logits_inf.iter()).enumerate() {
        assert!(
            (*a as f64 - b).abs() < 1e-4,
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
                    (x1 - x2).abs() < 1e-6,
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

#[test]
#[cfg(target_os = "macos")]
fn test_metal_training() {
    // Verify that the full train-forward-backward pipeline works on Metal GPU.
    // Uses minimal model size — just enough to exercise Metal matmul with
    // the multi-head attention reshape+permute path.

    let device = Device::new_metal(0).expect("Metal device should be available on macOS");

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 42;

    let model = TensorGpt::new(vocab_size, seed, config, &device);
    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 3,
        ..Default::default()
    };

    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
    let tokens = vec![1, 2, 3, 4, 5, 1, 2, 3];

    let mut losses = Vec::new();
    for i in 0..train_config.num_steps {
        let loss = tensor_train_step(&model, &tokens, &mut optimizer, &train_config, i).unwrap();
        assert!(loss.is_finite(), "loss should be finite at step {i}");
        losses.push(loss);
    }

    assert!(losses.last().unwrap() < losses.first().unwrap(), "loss should decrease over training");
}

#[test]
fn test_checkpoint_resume_roundtrip() {
    // Train N steps, checkpoint, resume for M more steps.
    // Compare with a single uninterrupted N+M step run.
    // Weights should match exactly.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 42;
    let device = Device::Cpu;
    let tokens = vec![1, 2, 3, 4, 5, 1, 2, 3];

    let n_first = 3;
    let n_second = 4;
    let total = n_first + n_second;

    let train_config_total = TrainConfig {
        learning_rate: 0.01,
        num_steps: total,
        ..Default::default()
    };

    // --- Single uninterrupted run ---
    let model_ref = TensorGpt::new(vocab_size, seed, config, &device);
    let mut opt_ref = TensorAdam::new(&model_ref.varmap, &train_config_total).unwrap();
    for i in 0..total {
        tensor_train_step(&model_ref, &tokens, &mut opt_ref, &train_config_total, i).unwrap();
    }
    let weights_ref = model_ref.save_weights();

    // --- Two-phase run with checkpoint/resume ---
    // Phase 1: train N steps
    let train_config_p1 = TrainConfig {
        learning_rate: 0.01,
        num_steps: total, // LR decay uses total steps
        ..Default::default()
    };
    let model_p1 = TensorGpt::new(vocab_size, seed, config, &device);
    let mut opt_p1 = TensorAdam::new(&model_p1.varmap, &train_config_p1).unwrap();
    for i in 0..n_first {
        tensor_train_step(&model_p1, &tokens, &mut opt_p1, &train_config_p1, i).unwrap();
    }

    // Save checkpoint
    let ckpt_weights = model_p1.save_weights();
    let ckpt_m = opt_p1.save_m();
    let ckpt_v = opt_p1.save_v();
    let ckpt_step = n_first;
    let ckpt_adam_step = opt_p1.step_t;

    // Phase 2: resume from checkpoint
    let model_p2 =
        TensorGpt::load_weights_with_config(vocab_size, &ckpt_weights, config, &device).unwrap();
    let train_config_p2 = TrainConfig {
        learning_rate: 0.01,
        num_steps: total,
        ..Default::default()
    };
    let mut opt_p2 = TensorAdam::new(&model_p2.varmap, &train_config_p2).unwrap();
    opt_p2
        .load_state(&ckpt_m, &ckpt_v, ckpt_adam_step)
        .unwrap();

    for i in ckpt_step..total {
        tensor_train_step(&model_p2, &tokens, &mut opt_p2, &train_config_p2, i).unwrap();
    }
    let weights_resumed = model_p2.save_weights();

    // --- Compare ---
    let snap_ref: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&weights_ref).unwrap();
    let snap_res: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&weights_resumed).unwrap();

    assert_eq!(snap_ref.len(), snap_res.len());
    for (k, v1) in &snap_ref {
        let v2 = snap_res
            .get(k)
            .unwrap_or_else(|| panic!("missing key {k} in resumed model"));
        for (row1, row2) in v1.iter().zip(v2.iter()) {
            for (x1, x2) in row1.iter().zip(row2.iter()) {
                assert!(
                    (x1 - x2).abs() < 1e-6,
                    "weight mismatch for key {k}: {x1} vs {x2}"
                );
            }
        }
    }

    // Also verify optimizer step counts match
    assert_eq!(opt_ref.step_t, opt_p2.step_t);
}

#[test]
fn test_optimizer_state_serialization_roundtrip() {
    // Verify that saving and loading optimizer state preserves m/v exactly.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 42;
    let device = Device::Cpu;
    let tokens = vec![1, 2, 3, 4, 5, 1, 2, 3];

    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 5,
        ..Default::default()
    };

    let model = TensorGpt::new(vocab_size, seed, config, &device);
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();

    // Train a few steps to populate m/v
    for i in 0..3 {
        tensor_train_step(&model, &tokens, &mut optimizer, &train_config, i).unwrap();
    }

    // Save optimizer state
    let m_json = optimizer.save_m();
    let v_json = optimizer.save_v();
    let step_t = optimizer.step_t;

    // Create a fresh optimizer and load state
    let mut optimizer2 = TensorAdam::new(&model.varmap, &train_config).unwrap();
    optimizer2.load_state(&m_json, &v_json, step_t).unwrap();

    // Verify step_t matches
    assert_eq!(optimizer2.step_t, step_t);

    // Verify m/v roundtrip by re-serializing
    let m_json2 = optimizer2.save_m();
    let v_json2 = optimizer2.save_v();

    let m1: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&m_json).unwrap();
    let m2: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&m_json2).unwrap();

    assert_eq!(m1.len(), m2.len());
    for (k, v1) in &m1 {
        let v2 = m2.get(k).unwrap();
        for (row1, row2) in v1.iter().zip(v2.iter()) {
            for (x1, x2) in row1.iter().zip(row2.iter()) {
                assert!(
                    (x1 - x2).abs() < 1e-6,
                    "m mismatch for key {k}: {x1} vs {x2}"
                );
            }
        }
    }

    let v1: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&v_json).unwrap();
    let v2: std::collections::HashMap<String, Vec<Vec<f64>>> =
        serde_json::from_str(&v_json2).unwrap();

    assert_eq!(v1.len(), v2.len());
    for (k, val1) in &v1 {
        let val2 = v2.get(k).unwrap();
        for (row1, row2) in val1.iter().zip(val2.iter()) {
            for (x1, x2) in row1.iter().zip(row2.iter()) {
                assert!(
                    (x1 - x2).abs() < 1e-6,
                    "v mismatch for key {k}: {x1} vs {x2}"
                );
            }
        }
    }
}
