package tracy

type Sphere struct {
	Center     Vec3
	Radius     float64
	Color      Color
	Specular   float64
	Reflective float64
	R2         float64 // radius squared, cached for performance
}

func NewSphere(center Vec3, radius float64, color Color, specular, reflective float64) Sphere {
	return Sphere{
		Center:     center,
		Radius:     radius,
		Color:      color,
		Specular:   specular,
		Reflective: reflective,
		R2:         radius * radius,
	}
}