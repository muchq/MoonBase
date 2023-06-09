package lib

import (
	"image"
	"image/color"
	"testing"
)

func TestGreyScaleImage(t *testing.T) {
	img := image.NewRGBA(image.Rectangle{
		Min: image.Point{X: 0, Y: 0},
		Max: image.Point{X: 3, Y: 3},
	})
	fillImage(*img, color.RGBA{R: 0, G: 100, B: 50, A: 255})

	grey := GreyScale(img)
	for _, b := range grey.Pix {
		if b != 64 {
			t.Fail()
		}
	}
}

func fillImage(img image.RGBA, rgba color.RGBA) {
	row := 0
	for row < img.Bounds().Dy() {
		col := 0
		for col < img.Bounds().Dx() {
			img.SetRGBA(row, col, rgba)
			col++
		}
		row++
	}
}
