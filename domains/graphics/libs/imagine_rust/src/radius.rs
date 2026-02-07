#[derive(Debug, Clone, Copy)]
pub enum Radius {
    Three,
    Five,
}

impl Radius {
    pub fn gaussian_kernel(&self) -> &'static [i32] {
        match self {
            Radius::Three => &[1, 3, 1, 3, 9, 3, 1, 3, 1],
            Radius::Five => &[
                1, 4, 7, 4, 1, 4, 16, 26, 16, 4, 7, 26, 41, 26, 7, 4, 16, 26, 16, 4, 1, 4, 7, 4, 1,
            ],
        }
    }
}
