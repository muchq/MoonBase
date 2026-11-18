use candle_core::Device;
use ncm::{
    data::Record,
    model::{Config, Model},
    pipeline,
};

fn config() -> Config {
    Config {
        vocab: 260,
        length: 8,
        prompt_length: 16,
        width: 8,
        heads: 2,
        layers: 1,
        codes: 8,
        scales: vec![1, 2, 4, 8],
    }
}

#[test]
fn invalid_scales_are_rejected() {
    let mut c = config();
    c.scales = vec![1, 3, 8];
    assert!(c.validate().is_err());
    c = config();
    c.heads = 0;
    assert!(c.validate().is_err());
}

#[test]
fn both_stages_learn_and_tokenizer_stays_frozen() -> ncm::Result<()> {
    let m = Model::new(config(), &Device::Cpu, 17)?;
    let target = vec![5, 6, 7, 8, 9, 10, 11, 2];
    let prompt = vec![3, 4];
    let before = m.reconstruction_loss(&target)?.to_scalar::<f32>()?;
    pipeline::fit_tokenizer(&m, &[target.clone()], 80, 0.01)?;
    let after = m.reconstruction_loss(&target)?.to_scalar::<f32>()?;
    assert!(after < before * 0.8, "{before} -> {after}");
    let frozen = m.encode(&target)?;
    let before = m.generator_loss(&prompt, &target)?.to_scalar::<f32>()?;
    pipeline::fit_generator(&m, &[(prompt.clone(), target.clone())], 80, 0.01)?;
    let after = m.generator_loss(&prompt, &target)?.to_scalar::<f32>()?;
    assert!(after < before * 0.5, "{before} -> {after}");
    assert_eq!(frozen, m.encode(&target)?);
    let generated = m.generate(&prompt)?;
    assert_eq!(generated.len(), 8);
    let dir = tempfile::tempdir()?;
    m.save(dir.path())?;
    let loaded = Model::load(dir.path(), &Device::Cpu)?;
    assert_eq!(generated, loaded.generate(&prompt)?);
    Ok(())
}

#[test]
fn data_preserves_code_and_rejects_empty_completion() {
    let r: Record = serde_json::from_str(
        r#"{"language":"java","prompt":"controller","completion":"// comment\nclass A {}"}"#,
    )
    .unwrap();
    assert!(r.validate().is_ok());
    assert!(r.completion.contains("// comment\n"));
    let r = Record {
        completion: "".into(),
        ..r
    };
    assert!(r.validate().is_err());
}

#[test]
fn checkpoint_pipeline_supports_languages_and_rejects_wrong_stage() -> ncm::Result<()> {
    let dir = tempfile::tempdir()?;
    let input = dir.path().join("train.jsonl");
    std::fs::write(
        &input,
        concat!(
            "{\"language\":\"java\",\"prompt\":\"type\",\"completion\":\"class A {}\"}\n",
            "{\"language\":\"go\",\"prompt\":\"type\",\"completion\":\"type A struct {}\"}\n"
        ),
    )?;
    let vq = dir.path().join("vq");
    let model = dir.path().join("model");
    let c = Config {
        vocab: 280,
        length: 32,
        prompt_length: 64,
        scales: vec![1, 2, 4, 8, 16, 32],
        ..config()
    };
    pipeline::train_tokenizer(&input, &vq, c, 2, 0.001, 42, &Device::Cpu)?;
    assert!(pipeline::LocalBackend::load(&vq, "java".into(), &Device::Cpu).is_err());
    pipeline::train_generator(&input, &vq, &model, 2, 0.001, &Device::Cpu)?;
    let original = Model::load(&vq, &Device::Cpu)?;
    let trained = Model::load(&model, &Device::Cpu)?;
    let left = original.tokenizer_vars.data().lock().unwrap();
    let right = trained.tokenizer_vars.data().lock().unwrap();
    for (name, value) in left.iter() {
        assert_eq!(
            value.flatten_all()?.to_vec1::<f32>()?,
            right[name].flatten_all()?.to_vec1::<f32>()?,
            "tokenizer changed: {name}"
        );
    }
    let metrics = pipeline::evaluate(&input, &model, &Device::Cpu)?;
    assert_eq!(metrics.examples, 2);
    assert!(metrics.generator_loss.is_finite());
    let backend = pipeline::LocalBackend::load(&model, "go".into(), &Device::Cpu)?;
    backend.generate("type")?;
    assert!(backend.generate(&"too long ".repeat(1000)).is_err());
    assert!(pipeline::train_generator(&input, &vq, &model, 1, 0.001, &Device::Cpu).is_err());
    Ok(())
}

#[test]
fn bpe_preserves_unseen_unicode_whitespace_and_comments() -> ncm::Result<()> {
    let records = vec![Record {
        language: "java".into(),
        prompt: "test".into(),
        completion: "class A {}".into(),
    }];
    let tokenizer = ncm::data::train_bpe(&records, 280)?;
    let text = "// λ 🦀\n\tclass B {\n}\n";
    let ids = ncm::data::encode(&tokenizer, text, 128, true)?;
    assert_eq!(ncm::data::decode(&tokenizer, &ids)?, text);
    assert!(ncm::data::encode(&tokenizer, text, 2, true).is_err());
    Ok(())
}

#[test]
fn corpus_collection_deduplicates_and_excludes_build_outputs() -> ncm::Result<()> {
    let dir = tempfile::tempdir()?;
    std::fs::write(dir.path().join("A.java"), "class A {}")?;
    std::fs::write(dir.path().join("copy.java"), "class A {}")?;
    std::fs::write(dir.path().join("B.go"), "package main")?;
    std::fs::create_dir(dir.path().join("target"))?;
    std::fs::write(dir.path().join("target/C.java"), "class C {}")?;
    let records = ncm::data::collect(dir.path(), "java", &["java".into()], 100)?;
    assert_eq!(records.len(), 1);
    assert_eq!(records[0].completion, "class A {}");
    Ok(())
}
