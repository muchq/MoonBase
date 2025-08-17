package tracy

type Color struct {
	R, G, B float64
}

func NewColor(r, g, b float64) Color {
	return Color{R: r, G: g, B: b}
}

func (c Color) Scale(s float64) Color {
	return Color{R: c.R * s, G: c.G * s, B: c.B * s}
}

func (c Color) Add(o Color) Color {
	return Color{R: c.R + o.R, G: c.G + o.G, B: c.B + o.B}
}

func ClampValue(v float64) uint8 {
	if v < 0 {
		return 0
	}
	if v > 255 {
		return 255
	}
	return uint8(v)
}