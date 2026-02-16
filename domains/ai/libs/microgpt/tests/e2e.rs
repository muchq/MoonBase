use microgpt::{Gpt, InferenceGpt, KvCache, ModelConfig, TrainConfig, Adam, train_step};
// We need to access InferenceKvCache from model module as it is not re-exported in lib.rs
use microgpt::model::InferenceKvCache;

#[test]
fn test_forward_pass_parity() {
    // 1. Forward pass parity
    // For a small model config and fixed seed, verify that Gpt (autograd) and
    // InferenceGpt (plain f64) produce the same logits for the same input

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 42;

    let gpt = Gpt::with_config(vocab_size, seed, config);
    let json = gpt.save_weights();
    let inference_gpt = InferenceGpt::load_weights_with_config(vocab_size, &json, config).unwrap();

    let mut kv_gpt = KvCache::new(&config);
    let mut kv_inf = InferenceKvCache::new(&config);

    let token_id = 1;
    let pos_id = 0;

    let logits_gpt = gpt.forward(token_id, pos_id, &mut kv_gpt);
    let logits_inf = inference_gpt.forward(token_id, pos_id, &mut kv_inf);

    assert_eq!(logits_gpt.len(), logits_inf.len());
    for (i, (v_gpt, v_inf)) in logits_gpt.iter().zip(logits_inf.iter()).enumerate() {
        assert!(
            (v_gpt.data() - v_inf).abs() < 1e-10,
            "mismatch at index {}: {} vs {}", i, v_gpt.data(), v_inf
        );
    }
}

#[test]
fn test_train_save_load_roundtrip() {
    // 2. Train → save → load roundtrip
    // Train for N steps, save weights/meta, reload into InferenceGpt,
    // verify generation produces expected output

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 42;

    let gpt = Gpt::with_config(vocab_size, seed, config);
    let mut adam = Adam::new(gpt.params().len());

    let train_config = TrainConfig {
        learning_rate: 0.01,
        num_steps: 5,
        ..Default::default()
    };

    // Dummy training data
    let tokens = vec![1, 2, 3, 4, 1, 2, 3, 4];

    for i in 0..train_config.num_steps {
        let params = gpt.params();
        let _loss = train_step(&gpt, &tokens, &params, &mut adam, &train_config, i);
    }

    let json = gpt.save_weights();
    let inference_gpt = InferenceGpt::load_weights_with_config(vocab_size, &json, config).unwrap();

    // Verify generation produces output (no crash) and is deterministic for same seed
    let seed_gen = 123;
    let output1 = inference_gpt.generate(1, 1.0, seed_gen, |id| Some((id as u8 + b'a') as char));
    let output2 = inference_gpt.generate(1, 1.0, seed_gen, |id| Some((id as u8 + b'a') as char));

    assert_eq!(output1, output2);
    assert!(!output1.is_empty());
}

#[test]
fn test_weight_serialization_roundtrip() {
    // 3. Weight serialization roundtrip
    // Save weights, reload them, confirm they match exactly

    let config = ModelConfig {
        n_embd: 16,
        n_head: 2,
        n_layer: 1,
        block_size: 8,
    };
    let vocab_size = 10;
    let seed = 99;

    let gpt = Gpt::with_config(vocab_size, seed, config);
    let json = gpt.save_weights();

    // Reload into a new Gpt instance
    let gpt2 = Gpt::load_weights_with_config(vocab_size, &json, config).unwrap();

    let params1 = gpt.params();
    let params2 = gpt2.params();

    assert_eq!(params1.len(), params2.len());

    for (i, (p1, p2)) in params1.iter().zip(params2.iter()).enumerate() {
        assert!(
            (p1.data() - p2.data()).abs() < 1e-10,
            "weight mismatch at index {}: {} vs {}", i, p1.data(), p2.data()
        );
    }
}

#[test]
fn test_deterministic_training() {
    // 4. Deterministic training
    // Two training runs with the same seed and config produce identical weights

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

    let run_training = || {
        let gpt = Gpt::with_config(vocab_size, seed, config);
        let mut adam = Adam::new(gpt.params().len());
        for i in 0..train_config.num_steps {
             let params = gpt.params();
             train_step(&gpt, &tokens, &params, &mut adam, &train_config, i);
        }
        gpt.save_weights()
    };

    let weights1 = run_training();
    let weights2 = run_training();

    // We can't compare JSON strings directly because HashMap iteration order is non-deterministic.
    // So we load them back and compare the values.
    let gpt1 = InferenceGpt::load_weights_with_config(vocab_size, &weights1, config).unwrap();
    let gpt2 = InferenceGpt::load_weights_with_config(vocab_size, &weights2, config).unwrap();

    for (k, v1) in &gpt1.state_dict {
        let v2 = gpt2.state_dict.get(k).expect("missing key in second run");
        for (row1, row2) in v1.iter().zip(v2.iter()) {
            for (x1, x2) in row1.iter().zip(row2.iter()) {
                 assert_eq!(x1, x2, "Weight mismatch for key {}", k);
            }
        }
    }
}
