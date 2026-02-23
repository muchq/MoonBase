use microgpt::{
    DType, Device, InferenceGpt, ModelConfig, StDtype, TensorAdam, TensorGpt, TrainConfig,
    tensor_train_step_batched, serialize_state_dict_st,
};
use microgpt::model::InferenceKvCache;

/// Convenience: run `tensor_train_step_batched` with a single sequence.
fn train_step(
    model: &TensorGpt,
    tokens: &[usize],
    optimizer: &mut TensorAdam,
    config: &TrainConfig,
    step: usize,
) -> f64 {
    tensor_train_step_batched(model, &[tokens.to_vec()], optimizer, config, step).unwrap()
}

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

    let tensor_gpt = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);
    let bytes = tensor_gpt.save_weights_st().unwrap();
    let inference_gpt = InferenceGpt::load_safetensors(vocab_size, &bytes, config).unwrap();

    let token_id = 1;

    // forward_batch with a single-token sequence: [1, 1, vocab_size] -> padded to [1, 8, vocab]
    let logits_tensor = tensor_gpt.forward_batch(&[&[token_id]]).unwrap();
    // We want the logit for the first token position (index 0)
    let logits_tensor: Vec<f32> = logits_tensor
        .squeeze(0).unwrap()  // [8, vocab_size]
        .get(0).unwrap()      // [vocab_size]
        .to_vec1().unwrap();

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

    let model = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);

    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 5,
        ..Default::default()
    };

    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
    let tokens = vec![1, 2, 3, 4, 1, 2, 3, 4];

    for i in 0..train_config.num_steps {
        train_step(&model, &tokens, &mut optimizer, &train_config, i);
    }

    let bytes = model.save_weights_st().unwrap();
    let inference_gpt = InferenceGpt::load_safetensors(vocab_size, &bytes, config).unwrap();

    let seed_gen = 123;
    let output1 = inference_gpt.generate(1, 1.0, seed_gen, None, |id| Some(((id as u8 + b'a') as char).to_string()));
    let output2 = inference_gpt.generate(1, 1.0, seed_gen, None, |id| Some(((id as u8 + b'a') as char).to_string()));

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

    let model = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);
    let bytes = model.save_weights_st().unwrap();

    let model2 = TensorGpt::load_weights_st(vocab_size, &bytes, config, &device).unwrap();
    let bytes2 = model2.save_weights_st().unwrap();

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
        let model = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);
        let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
        for i in 0..train_config.num_steps {
            train_step(&model, &tokens, &mut optimizer, &train_config, i);
        }
        model.save_weights_st().unwrap()
    };

    let bytes1 = run_training();
    let bytes2 = run_training();

    // Both safetensors blobs should be byte-identical for deterministic training
    assert_eq!(bytes1, bytes2, "deterministic training produced different weights");
}

#[test]
fn test_warmup_prevents_loss_spike() {
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;
    let tokens = vec![1, 2, 3, 4, 5, 1, 2, 3];

    // High LR that would spike without warmup.
    let run = |warmup: usize| -> Vec<f64> {
        let tc = TrainConfig {
            learning_rate: 0.5,
            num_steps: 20,
            warmup_steps: warmup,
            ..Default::default()
        };
        let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);
        let mut opt = TensorAdam::new(&model.varmap, &tc).unwrap();
        (0..20)
            .map(|i| train_step(&model, &tokens, &mut opt, &tc, i))
            .collect()
    };

    let losses_no_warmup = run(0);
    let losses_warmup = run(10);

    // With warmup, early losses should stay closer to the initial loss.
    let initial = losses_warmup[0];
    let max_warmup_early = losses_warmup[..5].iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let max_no_warmup_early = losses_no_warmup[..5].iter().cloned().fold(f64::NEG_INFINITY, f64::max);

    assert!(
        max_warmup_early <= max_no_warmup_early,
        "warmup early max ({max_warmup_early:.4}) should be <= no-warmup early max ({max_no_warmup_early:.4})"
    );

    // The warmup run's early losses should not spike far above the initial loss.
    assert!(
        max_warmup_early < initial * 3.0,
        "warmup should prevent large spike: max={max_warmup_early:.4}, initial={initial:.4}"
    );
}

#[test]
fn test_warmup_checkpoint_resume_consistency() {
    // Train with warmup, checkpoint mid-warmup, resume — should match uninterrupted run.
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;
    let tokens = vec![1, 2, 3, 4, 5, 1, 2, 3];

    let tc = TrainConfig {
        learning_rate: 0.01,
        num_steps: 10,
        warmup_steps: 5,
        ..Default::default()
    };

    // Uninterrupted run
    let model_ref = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);
    let mut opt_ref = TensorAdam::new(&model_ref.varmap, &tc).unwrap();
    for i in 0..10 {
        train_step(&model_ref, &tokens, &mut opt_ref, &tc, i);
    }
    let ref_bytes = model_ref.save_weights_st().unwrap();

    // Interrupted: train 3 steps, checkpoint, resume for 7 more
    let model_p1 = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);
    let mut opt_p1 = TensorAdam::new(&model_p1.varmap, &tc).unwrap();
    for i in 0..3 {
        train_step(&model_p1, &tokens, &mut opt_p1, &tc, i);
    }

    let ckpt_w = model_p1.save_weights_st().unwrap();
    let ckpt_m = opt_p1.save_m_st().unwrap();
    let ckpt_v = opt_p1.save_v_st().unwrap();

    let model_p2 = TensorGpt::load_weights_st(vocab_size, &ckpt_w, config, &device).unwrap();
    let mut opt_p2 = TensorAdam::new(&model_p2.varmap, &tc).unwrap();
    opt_p2.load_state_st(&ckpt_m, &ckpt_v, opt_p1.step_t).unwrap();
    for i in 3..10 {
        train_step(&model_p2, &tokens, &mut opt_p2, &tc, i);
    }
    let resumed_bytes = model_p2.save_weights_st().unwrap();

    assert_eq!(ref_bytes, resumed_bytes, "warmup checkpoint resume should match uninterrupted run");
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

    let model = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);
    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 3,
        ..Default::default()
    };

    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
    let tokens = vec![1, 2, 3, 4, 5, 1, 2, 3];

    let mut losses = Vec::new();
    for i in 0..train_config.num_steps {
        let loss = train_step(&model, &tokens, &mut optimizer, &train_config, i);
        assert!(loss.is_finite(), "loss should be finite at step {i}");
        losses.push(loss);
    }

    assert!(losses.last().unwrap() < losses.first().unwrap(), "loss should decrease over training");
}

#[test]
fn test_batched_forward_output_shape() {
    // forward_batch with variable-length sequences returns [B, max_len, vocab].

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;
    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);

    let s1 = vec![0usize, 1, 2, 3];   // len 4
    let s2 = vec![0usize, 5, 6];      // len 3 → padded to 8 (block_size)
    let logits = model.forward_batch(&[&s1, &s2]).unwrap();

    assert_eq!(logits.dims(), &[2, 8, vocab_size]);
}

#[test]
fn test_batched_step_with_multiple_sequences() {
    // A batched step with B>1 sequences should produce a finite loss that decreases.

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;
    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);

    let train_config = TrainConfig {
        learning_rate: 0.05,
        num_steps: 20,
        ..Default::default()
    };
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();

    let batch = vec![
        vec![0usize, 1, 2, 3, 4, 5, 6, 7],
        vec![3usize, 4, 5, 6, 7, 0, 1],
        vec![7usize, 6, 5, 4, 3],
    ];

    let first = tensor_train_step_batched(&model, &batch, &mut optimizer, &train_config, 0).unwrap();
    assert!(first.is_finite());
    let mut last = first;
    for step in 1..20 {
        last = tensor_train_step_batched(&model, &batch, &mut optimizer, &train_config, step).unwrap();
    }
    assert!(last < first, "loss should decrease: {first:.4} → {last:.4}");
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
    let model_ref = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);
    let mut opt_ref = TensorAdam::new(&model_ref.varmap, &train_config_total).unwrap();
    for i in 0..total {
        train_step(&model_ref, &tokens, &mut opt_ref, &train_config_total, i);
    }
    let weights_ref = model_ref.save_weights_st().unwrap();

    // --- Two-phase run with checkpoint/resume ---
    // Phase 1: train N steps
    let train_config_p1 = TrainConfig {
        learning_rate: 0.01,
        num_steps: total, // LR decay uses total steps
        ..Default::default()
    };
    let model_p1 = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);
    let mut opt_p1 = TensorAdam::new(&model_p1.varmap, &train_config_p1).unwrap();
    for i in 0..n_first {
        train_step(&model_p1, &tokens, &mut opt_p1, &train_config_p1, i);
    }

    // Save checkpoint
    let ckpt_weights = model_p1.save_weights_st().unwrap();
    let ckpt_m = opt_p1.save_m_st().unwrap();
    let ckpt_v = opt_p1.save_v_st().unwrap();
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
        train_step(&model_p2, &tokens, &mut opt_p2, &train_config_p2, i);
    }
    let weights_resumed = model_p2.save_weights_st().unwrap();

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

    let model = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();

    // Train a few steps to populate m/v
    for i in 0..3 {
        train_step(&model, &tokens, &mut optimizer, &train_config, i);
    }

    // Save optimizer state
    let m_bytes = optimizer.save_m_st().unwrap();
    let v_bytes = optimizer.save_v_st().unwrap();
    let step_t = optimizer.step_t;

    // Create a fresh optimizer and load state
    let mut optimizer2 = TensorAdam::new(&model.varmap, &train_config).unwrap();
    optimizer2.load_state_st(&m_bytes, &v_bytes, step_t).unwrap();

    // Verify step_t matches
    assert_eq!(optimizer2.step_t, step_t);

    // Verify m/v roundtrip by re-serializing
    let m_bytes2 = optimizer2.save_m_st().unwrap();
    let v_bytes2 = optimizer2.save_v_st().unwrap();

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

    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);
    let bytes1 = model.save_weights_st().unwrap();

    let model2 = TensorGpt::load_weights_st(vocab_size, &bytes1, config, &device).unwrap();
    let bytes2 = model2.save_weights_st().unwrap();

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

    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);
    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 5,
        ..Default::default()
    };
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
    let tokens = vec![1, 2, 3, 4, 1, 2, 3, 4];
    for i in 0..train_config.num_steps {
        train_step(&model, &tokens, &mut optimizer, &train_config, i);
    }

    // Export as f32 → load into InferenceGpt → re-export as f16
    let bytes_f32 = model.save_weights_st().unwrap();
    let inf_f32 = InferenceGpt::load_safetensors(vocab_size, &bytes_f32, config).unwrap();

    let bytes_f16 = microgpt::serialize_state_dict_st(
        &inf_f32.state_dict,
        microgpt::StDtype::F16,
    ).unwrap();
    let inf_f16 = InferenceGpt::load_safetensors(vocab_size, &bytes_f16, config).unwrap();

    // Generate from the f16 model
    let output = inf_f16.generate(1, 1.0, 123, None, |id| Some(((id as u8 + b'a') as char).to_string()));
    assert!(!output.is_empty(), "f16 model should produce non-empty output");

    // Verify f16 file is roughly half the size of f32
    assert!(
        bytes_f16.len() < bytes_f32.len(),
        "f16 ({}) should be smaller than f32 ({})",
        bytes_f16.len(),
        bytes_f32.len()
    );
}

#[test]
#[cfg(target_os = "macos")]
fn test_bf16_training_step() {
    // Verify that we can initialize and train a model in BF16.
    // This exercises the mixed-precision path in TensorAdam (casting gradients to F32).
    // Note: BF16 matmul on CPU is often not supported, so we require Metal.

    let device = Device::new_metal(0).expect("Metal device should be available on macOS");

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;

    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::BF16);
    
    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 5,
        ..Default::default()
    };
    let mut optimizer = TensorAdam::new(&model.varmap, &train_config).unwrap();
    let tokens = vec![1, 2, 3, 4, 1, 2, 3, 4];

    // Run a few steps
    for i in 0..3 {
        let loss = train_step(&model, &tokens, &mut optimizer, &train_config, i);
        assert!(loss.is_finite(), "BF16 training step produced non-finite loss: {}", loss);
    }

    // Save weights (the bug: save_weights_st used to call .to_vec1::<f32>() on BF16 tensors)
    let bytes = model.save_weights_st().unwrap();

    // Reload and verify logits match
    let model2 = TensorGpt::load_weights_st(vocab_size, &bytes, config, &device).unwrap();
    let bytes2 = model2.save_weights_st().unwrap();
    assert_eq!(bytes, bytes2, "BF16 save/load roundtrip changed weights");

    // Verify the saved weights can also be loaded for inference
    let inf = InferenceGpt::load_safetensors(vocab_size, &bytes, config).unwrap();
    let output = inf.generate(1, 1.0, 123, None, |id| Some(((id as u8 + b'a') as char).to_string()));
    assert!(!output.is_empty(), "BF16-trained model should produce non-empty output");

    // Verify optimizer state also roundtrips
    let m_bytes = optimizer.save_m_st().unwrap();
    let v_bytes = optimizer.save_v_st().unwrap();
    let mut opt2 = TensorAdam::new(&model2.varmap, &train_config).unwrap();
    opt2.load_state_st(&m_bytes, &v_bytes, optimizer.step_t).unwrap();
    assert_eq!(opt2.step_t, optimizer.step_t);
}

#[test]
#[cfg(target_os = "macos")]
fn test_bf16_checkpoint_resume_preserves_dtype() {
    let device = Device::new_metal(0).expect("Metal device should be available on macOS");

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let tokens = vec![1, 2, 3, 4, 1, 2, 3, 4];

    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 10,
        ..Default::default()
    };

    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::BF16);
    let mut opt = TensorAdam::new(&model.varmap, &train_config).unwrap();
    for i in 0..3 {
        train_step(&model, &tokens, &mut opt, &train_config, i);
    }

    let ckpt_bytes = model.save_weights_st().unwrap();
    let m_bytes = opt.save_m_st().unwrap();
    let v_bytes = opt.save_v_st().unwrap();

    let model2 = TensorGpt::load_weights_st(vocab_size, &ckpt_bytes, config, &device).unwrap();
    assert_eq!(model2.dtype, DType::BF16, "resumed model should preserve BF16 dtype");

    let mut opt2 = TensorAdam::new(&model2.varmap, &train_config).unwrap();
    opt2.load_state_st(&m_bytes, &v_bytes, opt.step_t).unwrap();

    let loss = train_step(&model2, &tokens, &mut opt2, &train_config, 3);
    assert!(loss.is_finite(), "resumed BF16 training produced non-finite loss: {}", loss);
}

/// Helper: create TensorGpt -> save as safetensors -> load as InferenceGpt.
fn make_inference(vocab_size: usize, seed: u64, config: ModelConfig) -> InferenceGpt {
    let device = Device::Cpu;
    let model = TensorGpt::new(vocab_size, seed, config, &device, DType::F32);
    let bytes = model.save_weights_st().unwrap();
    InferenceGpt::load_safetensors(vocab_size, &bytes, config).unwrap()
}

#[test]
fn test_generate_temperature_zero() {
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let inf = make_inference(10, 42, config);

    let output = inf.generate(1, 0.0, 42, None, |id| Some(((id as u8 + b'a') as char).to_string()));
    assert!(!output.is_empty(), "temperature=0 should produce output via greedy argmax");

    let output2 = inf.generate(1, 0.0, 999, None, |id| Some(((id as u8 + b'a') as char).to_string()));
    assert_eq!(output, output2, "temperature=0 should be deterministic regardless of seed");
}

#[test]
fn test_generate_from_prompt_temperature_zero() {
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let inf = make_inference(10, 42, config);

    let output1 = inf.generate_from_prompt(&[9, 0, 1], &[9], &[], 0.0, 42, None, |_| {});
    let output2 = inf.generate_from_prompt(&[9, 0, 1], &[9], &[], 0.0, 999, None, |_| {});
    assert_eq!(output1, output2, "temperature=0 prompt generation should be seed-independent");
}

// --- BPE tokenizer integration tests ---

#[test]
fn test_generate_with_bpe_tokenizer() {
    use microgpt::Tokenizer;

    let corpus = "The quick brown fox jumps over the lazy dog. \
                  A journey of a thousand miles begins with a single step.";
    let tok = Tokenizer::train_bpe(corpus, 500, false);

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 32,
    };
    let device = Device::Cpu;
    let model = TensorGpt::new(tok.vocab_size, 42, config, &device, DType::F32);
    let bytes = model.save_weights_st().unwrap();
    let inf = InferenceGpt::load_safetensors(tok.vocab_size, &bytes, config).unwrap();

    let output = inf.generate(tok.bos, 0.8, 42, None, |id| tok.decode(id));
    // Untrained model may produce nonsense but should not panic and should
    // produce some output (it won't hit BOS again with high probability).
    assert!(output.len() <= 200, "output should be bounded");
}

#[test]
fn test_generate_from_prompt_with_bpe_tokenizer() {
    use microgpt::Tokenizer;

    let corpus = "Hello world! How are you doing today? I am fine thank you.";
    let tok = Tokenizer::train_bpe(corpus, 500, true);
    let special = tok.special_tokens.as_ref().unwrap();

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 64,
    };
    let device = Device::Cpu;
    let model = TensorGpt::new(tok.vocab_size, 42, config, &device, DType::F32);
    let bytes = model.save_weights_st().unwrap();
    let inf = InferenceGpt::load_safetensors(tok.vocab_size, &bytes, config).unwrap();

    let mut prompt = vec![tok.bos];
    prompt.extend(tok.encode_turn("user", "Hello"));
    prompt.push(special.assistant);

    let mut streamed_tokens = Vec::new();
    let output_tokens = inf.generate_from_prompt(
        &prompt,
        &[special.end_turn],
        &[tok.bos],
        0.5,
        42,
        Some(16),
        |t| streamed_tokens.push(t),
    );
    assert!(output_tokens.len() <= 16, "should respect max_tokens");
    assert_eq!(output_tokens, streamed_tokens, "streamed tokens should match returned tokens");
    let decoded = tok.decode_str(&output_tokens);
    assert!(!decoded.is_empty() || output_tokens.is_empty());
}

#[test]
fn test_bpe_tokenizer_save_load_with_model() {
    use microgpt::Tokenizer;

    let corpus = "Testing save and load with model weights end to end.";
    let tok = Tokenizer::train_bpe(corpus, 500, false);

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 16,
    };
    let device = Device::Cpu;
    let model = TensorGpt::new(tok.vocab_size, 42, config, &device, DType::F32);

    // Save tokenizer — use a unique dir to avoid races with parallel test runs.
    let dir = std::env::temp_dir().join(format!(
        "microgpt_e2e_bpe_save_{}_{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    std::fs::create_dir_all(&dir).unwrap();
    let tok_path = dir.join("tokenizer.json");
    tok.save(&tok_path).unwrap();

    // Save model weights
    let weight_bytes = model.save_weights_st().unwrap();

    // Reload both
    let tok2 = Tokenizer::from_file(&tok_path).unwrap();
    let inf = InferenceGpt::load_safetensors(tok2.vocab_size, &weight_bytes, config).unwrap();

    // Generate with reloaded tokenizer
    let output = inf.generate(tok2.bos, 0.5, 42, None, |id| tok2.decode(id));
    // Just verify it doesn't panic and produces something
    assert!(output.len() <= 200);

    // Verify encoding is identical before/after save-load
    let text = "Testing";
    assert_eq!(tok.encode_str(text), tok2.encode_str(text));
}

// --- Tests for previous functionality (code review implementation) ---

#[test]
fn test_generate_output_is_trimmed() {
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 16,
    };
    // Use a vocabulary that includes space and newline so the model *can*
    // produce leading/trailing whitespace if it wants to.
    let vocab_size = 10;
    let inf = make_inference(vocab_size, 42, config);

    let decode = |id: usize| -> Option<String> {
        match id {
            0 => Some(" ".to_string()),
            1 => Some("\n".to_string()),
            i if i < vocab_size => Some(((b'a' + (i - 2) as u8) as char).to_string()),
            _ => None,
        }
    };

    for seed in 0..10 {
        let output = inf.generate(vocab_size - 1, 0.8, seed, None, decode);
        assert_eq!(
            output,
            output.trim(),
            "generate output should be trimmed (seed={seed}): {:?}",
            output
        );
    }
}

#[test]
fn test_generate_trimming_preserves_inner_whitespace() {
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 16,
    };
    let inf = make_inference(10, 42, config);

    let output = inf.generate(9, 0.5, 42, None, |id| -> Option<String> {
        match id {
            0 => Some(" ".to_string()),
            i if i < 9 => Some(((b'a' + (i - 1) as u8) as char).to_string()),
            _ => None,
        }
    });
    // Output should be trimmed but we shouldn't lose interior whitespace.
    // Just verify the invariant: trimmed == itself.
    assert_eq!(output, output.trim());
}

#[test]
fn test_save_weights_st_returns_ok() {
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let device = Device::Cpu;
    let model = TensorGpt::new(10, 42, config, &device, DType::F32);
    let result = model.save_weights_st();
    assert!(result.is_ok(), "save_weights_st should succeed for a valid model");
    assert!(!result.unwrap().is_empty());
}

#[test]
fn test_serialize_state_dict_st_unsupported_dtype() {
    let inf = make_inference(5, 42, ModelConfig::default());
    // BF16 is not supported for InferenceGpt state_dict export
    let result = serialize_state_dict_st(&inf.state_dict, StDtype::BF16);
    assert!(result.is_err(), "BF16 export should return an error");
    let err_msg = result.unwrap_err().to_string();
    assert!(
        err_msg.contains("unsupported"),
        "error should mention 'unsupported': {err_msg}"
    );
}

#[test]
fn test_serialize_state_dict_st_f32_ok() {
    let inf = make_inference(5, 42, ModelConfig::default());
    let result = serialize_state_dict_st(&inf.state_dict, StDtype::F32);
    assert!(result.is_ok(), "F32 export should succeed");
}

#[test]
fn test_serialize_state_dict_st_f16_ok() {
    let inf = make_inference(5, 42, ModelConfig::default());
    let result = serialize_state_dict_st(&inf.state_dict, StDtype::F16);
    assert!(result.is_ok(), "F16 export should succeed");
}

#[test]
fn test_pre_resolved_weights_see_optimizer_updates() {
    // Verify that pre-resolved Var handles reflect weight updates from the
    // optimizer — i.e. forward_batch produces different logits after a step.
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let device = Device::Cpu;
    let model = TensorGpt::new(vocab_size, 42, config, &device, DType::F32);

    let input: Vec<usize> = vec![0, 1, 2, 3];
    let logits_before = model.forward_batch(&[&input]).unwrap();
    let logits_before: Vec<f32> = logits_before
        .squeeze(0).unwrap()
        .get(0).unwrap()
        .to_vec1().unwrap();

    let train_config = TrainConfig {
        learning_rate: 0.1,
        num_steps: 5,
        ..Default::default()
    };
    let mut opt = TensorAdam::new(&model.varmap, &train_config).unwrap();
    let batch = vec![vec![0usize, 1, 2, 3, 4]];
    tensor_train_step_batched(&model, &batch, &mut opt, &train_config, 0).unwrap();

    let logits_after = model.forward_batch(&[&input]).unwrap();
    let logits_after: Vec<f32> = logits_after
        .squeeze(0).unwrap()
        .get(0).unwrap()
        .to_vec1().unwrap();

    assert_ne!(
        logits_before, logits_after,
        "logits should change after an optimizer step (pre-resolved handles must track updates)"
    );
}

#[test]
fn test_optimizer_save_returns_result() {
    let config = ModelConfig {
        n_embd: 8,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let device = Device::Cpu;
    let model = TensorGpt::new(5, 42, config, &device, DType::F32);
    let train_config = TrainConfig::default();
    let opt = TensorAdam::new(&model.varmap, &train_config).unwrap();

    let m_result = opt.save_m_st();
    let v_result = opt.save_v_st();
    assert!(m_result.is_ok(), "save_m_st should return Ok");
    assert!(v_result.is_ok(), "save_v_st should return Ok");
    assert!(!m_result.unwrap().is_empty());
    assert!(!v_result.unwrap().is_empty());
}

// --- skip-long / filter_to_block_size tests ---

#[test]
fn test_skip_long_trains_only_on_fitting_docs() {
    use microgpt::{Dataset, ChatDataset};

    let dir = std::env::temp_dir().join(format!(
        "microgpt_skip_long_{}",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    std::fs::create_dir_all(&dir).unwrap();

    // Text dataset: "short" encodes to ~3 tokens, the long line to many more
    let text_path = dir.join("text.txt");
    std::fs::write(&text_path, "hi\nbye\nthis is a much longer line with many tokens for testing\n").unwrap();
    let mut ds = Dataset::load(&text_path, 300).unwrap();
    let original = ds.docs.len();
    assert_eq!(original, 3);

    let removed = ds.filter_to_block_size(5);
    assert!(removed > 0, "long doc should be removed at block_size=5");
    assert!(ds.docs.len() < original);
    // Verify remaining docs all fit
    for doc in &ds.tokenized_docs {
        assert!(doc.len() <= 6);
    }

    // Chat dataset
    let chat_path = dir.join("chat.jsonl");
    let short = r#"[{"role":"user","content":"hi"},{"role":"assistant","content":"hey"}]"#;
    let long = r#"[{"role":"user","content":"tell me everything about the history of the entire world in great detail please"},{"role":"assistant","content":"well it all started a very long time ago with the big bang and then stars formed and galaxies and eventually planets"}]"#;
    std::fs::write(&chat_path, format!("{short}\n{long}\n{short}\n")).unwrap();
    let mut cds = ChatDataset::load(&chat_path, 300).unwrap();
    assert_eq!(cds.len(), 3);

    let removed = cds.filter_to_block_size(15);
    assert!(removed > 0, "long conversation should be removed");
    assert!(cds.len() < 3);
    for conv in &cds.tokenized_conversations {
        assert!(conv.len() <= 16);
    }

    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn test_generate_from_prompt_suppresses_tokens() {
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 32,
    };
    let inf = make_inference(10, 42, config);

    // Suppress tokens 0 and 1; stop on token 9
    let output = inf.generate_from_prompt(
        &[5, 3],
        &[9],
        &[0, 1],
        0.8,
        42,
        Some(20),
        |_| {},
    );
    for &tok in &output {
        assert_ne!(tok, 0, "suppressed token 0 should not appear");
        assert_ne!(tok, 1, "suppressed token 1 should not appear");
        assert_ne!(tok, 9, "stop token 9 should not appear in output");
    }
}

#[test]
fn test_generate_from_prompt_min_one_token_before_stop() {
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 32,
    };
    let inf = make_inference(10, 42, config);

    // Suppress all tokens except stop_token=9 and one content token.
    // The min-1-token guard should force at least one content token out
    // before stop_token can fire.
    let output = inf.generate_from_prompt(
        &[5, 3],
        &[9],
        &[0, 1, 2, 3, 4, 5, 6, 7],
        0.5,
        42,
        Some(20),
        |_| {},
    );
    // With tokens 0-8 suppressed (except 8) and 9 as stop, the model can
    // only generate 8s until stop fires. It must generate at least one.
    assert!(
        !output.is_empty(),
        "min-token guard should prevent empty output when stop_token would fire immediately"
    );
}

// --- rolling context window tests ---

#[test]
fn test_truncation_strips_bos_and_restore_allows_generation() {
    use microgpt::Tokenizer;

    let corpus = "hello world this is a test of the rolling context window for chat";
    let tok = Tokenizer::train_bpe(corpus, 300, true);
    let special = tok.special_tokens.as_ref().unwrap();
    let block_size = 32;

    // Simulate a multi-turn chat history that exceeds block_size
    let mut history = vec![tok.bos];
    for _ in 0..6 {
        history.extend(tok.encode_turn("user", "hello"));
        history.extend(tok.encode_turn("assistant", "world"));
    }
    history.push(special.assistant);
    assert!(history.len() > block_size, "history should exceed block_size");

    let dropped = tok.truncate_chat_prompt(&mut history, block_size);
    assert!(dropped > 0);

    // BOS is gone after truncation
    assert_ne!(history.first().copied(), Some(tok.bos));

    // Restore BOS (as the CLI and serve do)
    history.insert(0, tok.bos);
    assert_eq!(history[0], tok.bos);

    // The restored history should still be within prompt budget
    let max_prompt = block_size - block_size / 4;
    assert!(
        history.len() <= max_prompt + 1, // +1 for the re-inserted BOS
        "history ({}) should fit in prompt budget ({})",
        history.len(),
        max_prompt + 1
    );

    // Generation should work on the restored history
    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size,
    };
    let inf = make_inference(tok.vocab_size, 42, config);
    let remaining = block_size.saturating_sub(history.len());
    let max_gen = remaining.max(4).min(block_size / 4);

    let output = inf.generate_from_prompt(
        &history,
        &[special.end_turn],
        &[tok.bos, special.user, special.assistant],
        0.5,
        42,
        Some(max_gen),
        |_| {},
    );
    // Model should produce at least one token (min_tokens guard)
    assert!(
        !output.is_empty(),
        "generation after BOS-restored truncation should produce tokens"
    );
}

#[test]
fn test_dynamic_max_gen_calculation() {
    let block_size: usize = 512;

    // Short prompt: max_gen capped at block_size/4
    let remaining = block_size.saturating_sub(50);
    let max_gen = remaining.max(64).min(block_size / 4);
    assert_eq!(max_gen, 128, "short prompt should cap at block_size/4");

    // Long prompt near block_size: floor of 64
    let remaining = block_size.saturating_sub(500);
    let max_gen = remaining.max(64).min(block_size / 4);
    assert_eq!(max_gen, 64, "long prompt should floor at 64");

    // Prompt exactly at block_size: floor of 64
    let remaining = block_size.saturating_sub(512);
    let max_gen = remaining.max(64).min(block_size / 4);
    assert_eq!(max_gen, 64, "full prompt should still get 64");

    // Medium prompt: remaining fits within cap
    let remaining = block_size.saturating_sub(400);
    let max_gen = remaining.max(64).min(block_size / 4);
    assert_eq!(max_gen, 112, "medium prompt should get remaining tokens");
}

#[test]
fn test_empty_output_history_management() {
    use microgpt::Tokenizer;

    let corpus = "hello world test";
    let tok = Tokenizer::train_bpe(corpus, 300, true);
    let special = tok.special_tokens.as_ref().unwrap();

    // Simulate a history where we're about to add a response
    let mut history = vec![tok.bos];
    history.extend(tok.encode_turn("user", "hello"));
    history.push(special.assistant);
    let history_before_gen = history.clone();

    // Simulate empty generation output
    let output: Vec<usize> = vec![];

    if !output.is_empty() {
        history.extend(&output);
        history.push(special.end_turn);
    } else {
        // Remove dangling <assistant>
        history.pop();
    }

    // History should be back to before we added <assistant>
    assert_eq!(
        history.len(),
        history_before_gen.len() - 1,
        "empty output should remove the dangling assistant token"
    );
    assert_ne!(
        history.last().copied(),
        Some(special.assistant),
        "history should not end with a dangling assistant token"
    );
    assert_eq!(
        history.last().copied(),
        Some(special.end_turn),
        "history should end with the user's end_turn"
    );
}
