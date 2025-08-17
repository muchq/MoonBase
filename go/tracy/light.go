package tracy

type LightType int

const (
	Ambient LightType = iota
	Point
	Directional
)

type Light struct {
	Type      LightType
	Intensity float64
	Position  Vec3
}

func NewLight(lightType LightType, intensity float64, position Vec3) Light {
	return Light{
		Type:      lightType,
		Intensity: intensity,
		Position:  position,
	}
}