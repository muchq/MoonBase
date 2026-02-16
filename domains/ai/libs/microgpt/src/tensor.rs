use candle_core::{Device, Result, Tensor};

/// Convert a Candle Tensor to a 2D Vec<Vec<f64>> for JSON serialization.
/// Assumes the tensor is 2D.
pub fn tensor_to_vec2(tensor: &Tensor) -> Result<Vec<Vec<f64>>> {
    let (_rows, _cols) = tensor.dims2()?;
    tensor.to_vec2::<f64>()
}

/// Convert a 2D Vec<Vec<f64>> (from JSON) to a Candle Tensor.
pub fn vec2_to_tensor(data: &[Vec<f64>], device: &Device) -> Result<Tensor> {
    let rows = data.len();
    let cols = if rows > 0 { data[0].len() } else { 0 };
    // Flatten
    let flat: Vec<f64> = data.iter().flatten().cloned().collect();
    Tensor::from_vec(flat, (rows, cols), device)
}

/// Helper to initialize a tensor with normal distribution.
pub fn init_normal(rows: usize, cols: usize, std: f64, device: &Device) -> Result<Tensor> {
    Tensor::randn(0.0, std, (rows, cols), device)
}
