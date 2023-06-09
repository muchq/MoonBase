package lib

import (
	"image"
	"image/draw"
	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"
)

func GreyScale(img image.Image) image.Gray {
	cpy := image.NewGray(img.Bounds())
	draw.Draw(cpy, cpy.Bounds(), img, image.Point{X: 0, Y: 0}, draw.Src)
	return *cpy
}

func BoxBlur(img image.Image, radius int, depth int) image.Gray {
	gray := GreyScale(img)
	i := 0
	for i < depth {
		doBoxBlurX(gray, radius)
		doBoxBlurY(gray, radius)
		i++
	}
	return gray
}

func BoxBlurX(img image.Image, radius int, depth int) image.Gray {
	gray := GreyScale(img)
	i := 0
	for i < depth {
		doBoxBlurX(gray, radius)
		i++
	}
	return gray
}

func BoxBlurY(img image.Image, radius int, depth int) image.Gray {
	gray := GreyScale(img)
	i := 0
	for i < depth {
		doBoxBlurY(gray, radius)
		i++
	}
	return gray
}

func doBoxBlurX(gray image.Gray, radius int) {
	pixels := gray.Pix
	divisor := uint8(2*radius + 1)

	row := 0
	height := gray.Bounds().Dy()
	for row < height {
		col := radius
		for col < gray.Stride-radius {
			center := row*gray.Stride + col
			newVal := pixels[center] / divisor
			r := 1
			for r <= radius {
				newVal = newVal + pixels[center+r]/divisor
				newVal = newVal + pixels[center-r]/divisor
				r++
			}
			pixels[center] = newVal
			col++
		}
		row++
	}
}

func doBoxBlurY(gray image.Gray, radius int) {
	pixels := gray.Pix
	divisor := uint8(2*radius + 1)

	row := radius
	height := gray.Bounds().Dy()
	for row < height-radius {
		col := 0
		for col < gray.Stride {
			center := row*gray.Stride + col
			newVal := pixels[center] / divisor
			r := 1
			for r <= radius {
				newVal = newVal + pixels[(row+r)*gray.Stride+col]/divisor
				newVal = newVal + pixels[(row-r)*gray.Stride+col]/divisor
				r++
			}
			pixels[center] = newVal
			col++
		}
		row++
	}
}

func copyGray(img image.Gray) image.Gray {
	cpy := image.NewGray(img.Bounds())
	draw.Draw(cpy, cpy.Bounds(), &img, image.Point{0, 0}, draw.Src)
	return *cpy
}
