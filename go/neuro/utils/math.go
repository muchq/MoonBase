package utils

import (
	"math"
	"math/rand"
)

func Sqrt(x float64) float64 {
	return math.Sqrt(x)
}

func RandomFloat64() float64 {
	return rand.Float64()
}

func Abs(x float64) float64 {
	return math.Abs(x)
}

func Max(a, b float64) float64 {
	if a > b {
		return a
	}
	return b
}

func Min(a, b float64) float64 {
	if a < b {
		return a
	}
	return b
}

func Clip(x, min, max float64) float64 {
	if x < min {
		return min
	}
	if x > max {
		return max
	}
	return x
}