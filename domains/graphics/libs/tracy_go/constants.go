package tracy

import "math"

var (
	Black  = Color{0, 0, 0}
	Red    = Color{255, 0, 0}
	Green  = Color{0, 255, 0}
	Blue   = Color{0, 0, 255}
	Yellow = Color{255, 255, 0}
	White  = Color{255, 255, 255}

	Background = Black
	
	Inf     = math.Inf(1)
	Epsilon = 0.0001
)