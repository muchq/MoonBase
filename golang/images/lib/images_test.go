package lib

import (
	"image"
	"image/color"
	"testing"
)

func TestGreyScaleImage(t *testing.T) {
	img := newRBGA(3, 3)
	fillRGBA(*img, color.RGBA{R: 0, G: 100, B: 50, A: 255})

	grey := GreyScale(img)
	for _, b := range grey.Pix {
		if b != 64 {
			t.Fail()
		}
	}
}

func TestBoxBlurX(t *testing.T) {
	img := newGray(4, 1, color.Gray{Y: 0})
	copy(img.Pix, []uint8{3, 9, 12, 3})

	blurred := BoxBlurGrayX(img, 1, 1)
	blurredPixels := blurred.Pix

	expectedPixels := []uint8{3, 8, 8, 3}

	i := 0
	for i < len(blurredPixels) {
		if blurredPixels[i] != expectedPixels[i] {
			t.Fatal("expected", expectedPixels, "actual", blurredPixels)
		}
		i++
	}
}

func TestBoxBlurY(t *testing.T) {
	img := newGray(1, 4, color.Gray{Y: 0})
	copy(img.Pix, []uint8{3, 9, 12, 3})

	blurred := BoxBlurGrayY(img, 1, 1)
	blurredPixels := blurred.Pix

	expectedPixels := []uint8{3, 8, 8, 3}

	i := 0
	for i < len(blurredPixels) {
		if blurredPixels[i] != expectedPixels[i] {
			t.Fatal("expected", expectedPixels, "actual", blurredPixels)
		}
		i++
	}
}

func newRBGA(width int, height int) *image.RGBA {
	return image.NewRGBA(image.Rectangle{
		Min: image.Point{X: 0, Y: 0},
		Max: image.Point{X: width, Y: height},
	})
}

func newGray(width int, height int, gray color.Gray) image.Gray {
	img := image.NewGray(image.Rectangle{
		Min: image.Point{X: 0, Y: 0},
		Max: image.Point{X: width, Y: height},
	})
	pixels := img.Pix
	i := 0
	for i < len(pixels) {
		pixels[i] = gray.Y
		i++
	}

	return *img
}

func fillRGBA(img image.RGBA, rgba color.RGBA) {
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
