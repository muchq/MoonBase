package tracy

type Scene struct {
	ViewportSize     float64
	ProjectionPlane  float64
	BackgroundColor  Color
	Spheres          []Sphere
	Lights           []Light
}

func NewScene(viewportSize, projectionPlane float64, backgroundColor Color, spheres []Sphere, lights []Light) Scene {
	return Scene{
		ViewportSize:     viewportSize,
		ProjectionPlane:  projectionPlane,
		BackgroundColor:  backgroundColor,
		Spheres:          spheres,
		Lights:           lights,
	}
}