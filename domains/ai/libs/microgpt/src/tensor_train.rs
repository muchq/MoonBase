use candle_core::{Result, Tensor, Var};
use crate::tensor_model::TensorGpt;
use crate::train::TrainConfig;

/// Manual Adam implementation matching microgpt's logic.
pub struct TensorAdam {
    params: Vec<Var>,
    m: Vec<Var>,
    v: Vec<Var>,
    config: TrainConfig,
}

impl TensorAdam {
    pub fn new(params: Vec<Var>, config: TrainConfig) -> Result<Self> {
        let mut m = Vec::with_capacity(params.len());
        let mut v = Vec::with_capacity(params.len());
        for p in &params {
             let shape = p.shape();
             let dtype = p.dtype();
             let device = p.device();
             m.push(Var::from_tensor(&Tensor::zeros(shape, dtype, device)?)?);
             v.push(Var::from_tensor(&Tensor::zeros(shape, dtype, device)?)?);
        }
        Ok(Self { params, m, v, config })
    }

    pub fn step(&mut self, loss: &Tensor, step: usize) -> Result<()> {
        let grads = loss.backward()?;
        let cfg = &self.config;

        // Learning rate schedule
        let lr = cfg.learning_rate * (1.0 - step as f64 / cfg.num_steps as f64);

        let beta1 = cfg.beta1;
        let beta2 = cfg.beta2;
        let eps = cfg.eps;

        let fix1 = 1.0 - beta1.powi((step + 1) as i32);
        let fix2 = 1.0 - beta2.powi((step + 1) as i32);

        for (i, param) in self.params.iter().enumerate() {
            if let Some(grad) = grads.get(param) {
                let m = &self.m[i];
                let v = &self.v[i];

                // m = b1*m + (1-b1)*g
                let m_new = ((m.as_tensor() * beta1)? + (grad * (1.0 - beta1))?)?;
                m.set(&m_new)?;

                // v = b2*v + (1-b2)*g^2
                let g2 = grad.sqr()?;
                let v_new = ((v.as_tensor() * beta2)? + (g2 * (1.0 - beta2))?)?;
                v.set(&v_new)?;

                // m_hat = m / fix1
                let m_hat = (m.as_tensor() / fix1)?;
                let v_hat = (v.as_tensor() / fix2)?;

                // update = lr * m_hat / (sqrt(v_hat) + eps)
                let denom = (v_hat.sqrt()? + eps)?;
                let update = ((m_hat / denom)? * lr)?;

                let p_new = (param.as_tensor() - update)?;
                param.set(&p_new)?;
            }
        }
        Ok(())
    }
}

/// Run a single training step on a batch of tokens.
pub fn train_step(
    model: &TensorGpt,
    tokens: &Tensor,
    optimizer: &mut TensorAdam,
    step: usize,
) -> Result<f64> {
    let (_b, t) = tokens.dims2()?;
    if t < 2 {
        return Ok(0.0);
    }

    let input = tokens.narrow(1, 0, t - 1)?;
    let target = tokens.narrow(1, 1, t - 1)?;

    let (loss, _logits) = model.forward(&input, Some(&target))?;
    let loss = loss.ok_or_else(|| candle_core::Error::Msg("Loss not computed".into()))?;

    optimizer.step(&loss, step)?;

    loss.to_scalar::<f64>()
}
