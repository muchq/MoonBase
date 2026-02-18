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
    let bytes = tensor_gpt.save_weights_st();
    let inference_gpt = InferenceGpt::load_safetensors(vocab_size, &bytes, config).unwrap();

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

    let bytes = model.save_weights_st();
    let inference_gpt = InferenceGpt::load_safetensors(vocab_size, &bytes, config).unwrap();

    let seed_gen = 123;
    let output1 = inference_gpt.generate(1, 1.0, seed_gen, None, |id| Some((id as u8 + b'a') as char));
    let output2 = inference_gpt.generate(1, 1.0, seed_gen, None, |id| Some((id as u8 + b'a') as char));

    assert_eq!(output1, output2);
    assert!(!output1.is_empty());
}

#[test]
fn test_weight_serialization_roundtrip() {
    // Save TensorGpt weights as safetensors, reload them, confirm logits match.

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
    let bytes = model.save_weights_st();

    let model2 = TensorGpt::load_weights_st(vocab_size, &bytes, config, &device).unwrap();
    let bytes2 = model2.save_weights_st();

    // Compare by loading both as InferenceGpt and checking logits
    let inf1 = InferenceGpt::load_safetensors(vocab_size, &bytes, config).unwrap();
    let inf2 = InferenceGpt::load_safetensors(vocab_size, &bytes2, config).unwrap();

    let mut kv1 = InferenceKvCache::new(&config);
    let mut kv2 = InferenceKvCache::new(&config);
    let logits1 = inf1.forward(1, 0, &mut kv1);
    let logits2 = inf2.forward(1, 0, &mut kv2);

    for (a, b) in logits1.iter().zip(logits2.iter()) {
        assert!(
            (a - b).abs() < 1e-6,
            "logits mismatch: {} vs {}", a, b
        );
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
        model.save_weights_st()
    };

    let bytes1 = run_training();
    let bytes2 = run_training();

    // Both safetensors blobs should be byte-identical for deterministic training
    assert_eq!(bytes1, bytes2, "deterministic training produced different weights");
}

#[test]
#[cfg(target_os = "macos")]
fn test_metal_training() {
    // Verify that the full train-forward-backward pipeline works on Metal GPU.

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
    let weights_ref = model_ref.save_weights_st();

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
    let ckpt_weights = model_p1.save_weights_st();
    let ckpt_m = opt_p1.save_m_st();
    let ckpt_v = opt_p1.save_v_st();
    let ckpt_adam_step = opt_p1.step_t;

    // Phase 2: resume from checkpoint
    let model_p2 =
        TensorGpt::load_weights_st(vocab_size, &ckpt_weights, config, &device).unwrap();
    let train_config_p2 = TrainConfig {
        learning_rate: 0.01,
        num_steps: total,
        ..Default::default()
    };
    let mut opt_p2 = TensorAdam::new(&model_p2.varmap, &train_config_p2).unwrap();
    opt_p2
        .load_state_st(&ckpt_m, &ckpt_v, ckpt_adam_step)
        .unwrap();

    for i in n_first..total {
        tensor_train_step(&model_p2, &tokens, &mut opt_p2, &train_config_p2, i).unwrap();
    }
    let weights_resumed = model_p2.save_weights_st();

    // Compare: both safetensors blobs should be identical
    assert_eq!(weights_ref, weights_resumed, "checkpoint resume produced different weights");

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
    let m_bytes = optimizer.save_m_st();
    let v_bytes = optimizer.save_v_st();
    let step_t = optimizer.step_t;

    // Create a fresh optimizer and load state
    let mut optimizer2 = TensorAdam::new(&model.varmap, &train_config).unwrap();
    optimizer2.load_state_st(&m_bytes, &v_bytes, step_t).unwrap();

    // Verify step_t matches
    assert_eq!(optimizer2.step_t, step_t);

    // Verify m/v roundtrip by re-serializing
    let m_bytes2 = optimizer2.save_m_st();
    let v_bytes2 = optimizer2.save_v_st();

    assert_eq!(m_bytes, m_bytes2, "optimizer m state changed after roundtrip");
    assert_eq!(v_bytes, v_bytes2, "optimizer v state changed after roundtrip");
}

#[test]
fn test_safetensors_save_load_byte_identity() {
    // save_weights_st → load_weights_st → save_weights_st should produce
    // identical bytes, guarding against non-deterministic serialization order.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;

    let model = TensorGpt::new(vocab_size, 42, config, &device);
    let bytes1 = model.save_weights_st();

    let model2 = TensorGpt::load_weights_st(vocab_size, &bytes1, config, &device).unwrap();
    let bytes2 = model2.save_weights_st();

    assert_eq!(bytes1, bytes2, "safetensors roundtrip changed bytes");
}

#[test]
fn test_f16_export_generates_valid_output() {
    // Train a model, export as f16 via serialize_state_dict_st, reload,
    // and verify generation produces non-empty output without panicking.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;

    let model = TensorGpt::new(vocab_size, 42, config, &device);
    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 5,
        ..Default::default()
    };
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
    let tokens = vec![1, 2, 3, 4, 1, 2, 3, 4];
    for i in 0..train_config.num_steps {
        tensor_train_step(&model, &tokens, &mut optimizer, &train_config, i).unwrap();
    }

    // Export as f32 → load into InferenceGpt → re-export as f16
    let bytes_f32 = model.save_weights_st();
    let inf_f32 = InferenceGpt::load_safetensors(vocab_size, &bytes_f32, config).unwrap();

    let bytes_f16 = microgpt::serialize_state_dict_st(
        &inf_f32.state_dict,
        microgpt::StDtype::F16,
    );
    let inf_f16 = InferenceGpt::load_safetensors(vocab_size, &bytes_f16, config).unwrap();

    // Generate from the f16 model
    let output = inf_f16.generate(1, 1.0, 123, None, |id| Some((id as u8 + b'a') as char));
    assert!(!output.is_empty(), "f16 model should produce non-empty output");

    // Verify f16 file is roughly half the size of f32
    assert!(
        bytes_f16.len() < bytes_f32.len(),
        "f16 ({}) should be smaller than f32 ({})",
        bytes_f16.len(),
        bytes_f32.len()
    );
}
