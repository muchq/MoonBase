package lib

import (
	"image"
	"testing"
)

func TestGreyScaleImage(t *testing.T) {
	img := image.NewRGBA(image.Rectangle{
		Min: image.Point{X: 0, Y: 0},
		Max: image.Point{X: 10, Y: 5},
	})
	pixelComponents := img.Pix
	i := 0
	for i < len(pixelComponents) {
		modFour := i % 4
		if modFour == 0 {
			pixelComponents[i] = 0 // R
		} else if modFour == 1 {
			pixelComponents[i] = 100 // G
		} else if modFour == 2 {
			pixelComponents[i] = 50 // B
		} else {
			pixelComponents[i] = 255 // A
		}
		i++
	}

	grey := GreyScale(img)
	for _, b := range grey.Pix {
		if b != 64 {
			t.Fail()
		}
	}
}
