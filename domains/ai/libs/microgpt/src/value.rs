use std::cell::RefCell;
use std::collections::HashSet;
use std::rc::Rc;

/// A scalar value that tracks the computational graph for automatic differentiation.
///
/// This is a faithful Rust port of the Python `Value` class from microgpt.py,
/// preserving the same eager-mode autograd semantics.
#[derive(Clone)]
pub struct Value(Rc<RefCell<ValueInner>>);

struct ValueInner {
    data: f64,
    grad: f64,
    children: Vec<Value>,
    local_grads: Vec<f64>,
}

impl Value {
    pub fn new(data: f64) -> Self {
        Value(Rc::new(RefCell::new(ValueInner {
            data,
            grad: 0.0,
            children: Vec::new(),
            local_grads: Vec::new(),
        })))
    }

    fn from_op(data: f64, children: Vec<Value>, local_grads: Vec<f64>) -> Self {
        Value(Rc::new(RefCell::new(ValueInner {
            data,
            grad: 0.0,
            children,
            local_grads,
        })))
    }

    pub fn data(&self) -> f64 {
        self.0.borrow().data
    }

    pub fn grad(&self) -> f64 {
        self.0.borrow().grad
    }

    pub fn set_data(&self, val: f64) {
        self.0.borrow_mut().data = val;
    }

    pub fn set_grad(&self, val: f64) {
        self.0.borrow_mut().grad = val;
    }

    pub fn add(&self, other: &Value) -> Value {
        Value::from_op(
            self.data() + other.data(),
            vec![self.clone(), other.clone()],
            vec![1.0, 1.0],
        )
    }

    pub fn mul(&self, other: &Value) -> Value {
        let sd = self.data();
        let od = other.data();
        Value::from_op(sd * od, vec![self.clone(), other.clone()], vec![od, sd])
    }

    pub fn pow(&self, exp: f64) -> Value {
        let sd = self.data();
        Value::from_op(
            sd.powf(exp),
            vec![self.clone()],
            vec![exp * sd.powf(exp - 1.0)],
        )
    }

    pub fn log(&self) -> Value {
        let sd = self.data();
        Value::from_op(sd.ln(), vec![self.clone()], vec![1.0 / sd])
    }

    pub fn exp(&self) -> Value {
        let sd = self.data();
        let e = sd.exp();
        Value::from_op(e, vec![self.clone()], vec![e])
    }

    pub fn relu(&self) -> Value {
        let sd = self.data();
        Value::from_op(
            sd.max(0.0),
            vec![self.clone()],
            vec![if sd > 0.0 { 1.0 } else { 0.0 }],
        )
    }

    pub fn neg(&self) -> Value {
        self.mul_scalar(-1.0)
    }

    pub fn sub(&self, other: &Value) -> Value {
        self.add(&other.neg())
    }

    pub fn div(&self, other: &Value) -> Value {
        self.mul(&other.pow(-1.0))
    }

    pub fn add_scalar(&self, s: f64) -> Value {
        self.add(&Value::new(s))
    }

    pub fn mul_scalar(&self, s: f64) -> Value {
        self.mul(&Value::new(s))
    }

    /// Reverse-mode automatic differentiation via topological sort.
    pub fn backward(&self) {
        let mut topo = Vec::new();
        let mut visited = HashSet::new();
        build_topo(self, &mut topo, &mut visited);

        self.0.borrow_mut().grad = 1.0;

        for v in topo.iter().rev() {
            let updates: Vec<(Value, f64)> = {
                let inner = v.0.borrow();
                inner
                    .children
                    .iter()
                    .zip(inner.local_grads.iter())
                    .map(|(child, &lg)| (child.clone(), lg * inner.grad))
                    .collect()
            };
            for (child, delta) in updates {
                child.0.borrow_mut().grad += delta;
            }
        }
    }

    fn ptr_key(&self) -> usize {
        Rc::as_ptr(&self.0) as usize
    }
}

fn build_topo(v: &Value, topo: &mut Vec<Value>, visited: &mut HashSet<usize>) {
    let key = v.ptr_key();
    if visited.contains(&key) {
        return;
    }
    visited.insert(key);
    let children: Vec<Value> = v.0.borrow().children.clone();
    for child in &children {
        build_topo(child, topo, visited);
    }
    topo.push(v.clone());
}

/// Sum a slice of Values into a single Value.
pub fn sum_values(vals: &[Value]) -> Value {
    assert!(!vals.is_empty());
    let mut acc = vals[0].clone();
    for v in &vals[1..] {
        acc = acc.add(v);
    }
    acc
}

/// Numerically stable softmax over a slice of Values.
pub fn softmax(logits: &[Value]) -> Vec<Value> {
    let max_val = logits
        .iter()
        .map(|v| v.data())
        .fold(f64::NEG_INFINITY, f64::max);
    let exps: Vec<Value> = logits.iter().map(|v| v.add_scalar(-max_val).exp()).collect();
    let total = sum_values(&exps);
    exps.iter().map(|e| e.div(&total)).collect()
}

/// RMS normalization over a vector of Values.
pub fn rmsnorm(x: &[Value]) -> Vec<Value> {
    let n = x.len() as f64;
    let ms = sum_values(&x.iter().map(|xi| xi.mul(xi)).collect::<Vec<_>>()).mul_scalar(1.0 / n);
    let scale = ms.add_scalar(1e-5).pow(-0.5);
    x.iter().map(|xi| xi.mul(&scale)).collect()
}

/// Linear transform: y = W * x (each row of w dot-product with x).
pub fn linear(x: &[Value], w: &[Vec<Value>]) -> Vec<Value> {
    w.iter()
        .map(|row| sum_values(&row.iter().zip(x.iter()).map(|(wi, xi)| wi.mul(xi)).collect::<Vec<_>>()))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_add_backward() {
        let a = Value::new(2.0);
        let b = Value::new(3.0);
        let c = a.add(&b);
        c.backward();
        assert!((c.data() - 5.0).abs() < 1e-10);
        assert!((a.grad() - 1.0).abs() < 1e-10);
        assert!((b.grad() - 1.0).abs() < 1e-10);
    }

    #[test]
    fn test_mul_backward() {
        let a = Value::new(2.0);
        let b = Value::new(3.0);
        let c = a.mul(&b);
        c.backward();
        assert!((c.data() - 6.0).abs() < 1e-10);
        assert!((a.grad() - 3.0).abs() < 1e-10);
        assert!((b.grad() - 2.0).abs() < 1e-10);
    }

    #[test]
    fn test_composite_backward() {
        let a = Value::new(2.0);
        let b = Value::new(3.0);
        // (a * b + a) => d/da = b + 1 = 4, d/db = a = 2
        let c = a.mul(&b).add(&a);
        c.backward();
        assert!((a.grad() - 4.0).abs() < 1e-10);
        assert!((b.grad() - 2.0).abs() < 1e-10);
    }

    #[test]
    fn test_softmax_sums_to_one() {
        let logits: Vec<Value> = vec![Value::new(1.0), Value::new(2.0), Value::new(3.0)];
        let probs = softmax(&logits);
        let total: f64 = probs.iter().map(|p| p.data()).sum();
        assert!((total - 1.0).abs() < 1e-8);
    }

    #[test]
    fn test_rmsnorm_scale() {
        let x = vec![Value::new(3.0), Value::new(4.0)];
        let normed = rmsnorm(&x);
        // ms = (9+16)/2 = 12.5, scale = 1/sqrt(12.5+1e-5) ≈ 0.2828
        let ms = (9.0 + 16.0) / 2.0;
        let scale = (ms + 1e-5_f64).powf(-0.5);
        assert!((normed[0].data() - 3.0 * scale).abs() < 1e-6);
        assert!((normed[1].data() - 4.0 * scale).abs() < 1e-6);
    }
}
