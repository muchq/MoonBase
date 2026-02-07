package lib

import (
	"image"
	"image/draw"
	_ "image/gif"
	_ "image/jpeg"
	"image/png"
	_ "image/png"
	"os"
)

func ReadImage(path string) (image.Image, string, error) {
	existingImageFile, err := os.Open(path)
	if err != nil {
		return nil, "", err
	}
	defer existingImageFile.Close()
	return image.Decode(existingImageFile)
}

func WriteImageAsPng(img image.Image, path string) error {
	oFile, _ := os.Create(path)
	defer oFile.Close()
	return png.Encode(oFile, img)
}

func GreyScale(img image.Image) image.Gray {
	cpy := image.NewGray(img.Bounds())
	draw.Draw(cpy, cpy.Bounds(), img, image.Point{X: 0, Y: 0}, draw.Src)
	return *cpy
}

func BoxBlur(img image.Image, radius int, depth int) image.Gray {
	gray := GreyScale(img)
	i := 0
	for i < depth {
		gray = doBoxBlurX(gray, radius)
		gray = doBoxBlurY(gray, radius)
		i++
	}
	return gray
}

func BoxBlurX(img image.Image, radius int, depth int) image.Gray {
	return BoxBlurGrayX(GreyScale(img), radius, depth)
}

func BoxBlurGrayX(img image.Gray, radius int, depth int) image.Gray {
	gray := img
	i := 0
	for i < depth {
		gray = doBoxBlurX(gray, radius)
		i++
	}
	return gray
}

func BoxBlurY(img image.Image, radius int, depth int) image.Gray {
	return BoxBlurGrayY(GreyScale(img), radius, depth)
}

func BoxBlurGrayY(img image.Gray, radius int, depth int) image.Gray {
	gray := img
	i := 0
	for i < depth {
		gray = doBoxBlurY(gray, radius)
		i++
	}
	return gray
}

func doBoxBlurX(gray image.Gray, radius int) image.Gray {
	grayCopy := copyGray(gray)
	originalPixels := gray.Pix
	copyPixels := grayCopy.Pix
	divisor := uint8(2*radius + 1)

	row := 0
	height := gray.Bounds().Dy()
	for row < height {
		col := radius
		for col < gray.Stride-radius {
			center := row*gray.Stride + col
			newVal := originalPixels[center] / divisor
			r := 1
			for r <= radius {
				newVal = newVal + originalPixels[center+r]/divisor
				newVal = newVal + originalPixels[center-r]/divisor
				r++
			}
			copyPixels[center] = newVal
			col++
		}
		row++
	}

	return grayCopy
}

func doBoxBlurY(gray image.Gray, radius int) image.Gray {
	grayCopy := copyGray(gray)
	originalPixels := gray.Pix
	copyPixels := grayCopy.Pix
	divisor := uint8(2*radius + 1)

	row := radius
	height := gray.Bounds().Dy()
	for row < height-radius {
		col := 0
		for col < gray.Stride {
			center := row*gray.Stride + col
			newVal := originalPixels[center] / divisor
			r := 1
			for r <= radius {
				newVal = newVal + originalPixels[(row+r)*gray.Stride+col]/divisor
				newVal = newVal + originalPixels[(row-r)*gray.Stride+col]/divisor
				r++
			}
			copyPixels[center] = newVal
			col++
		}
		row++
	}

	return grayCopy
}

func copyGray(img image.Gray) image.Gray {
	cpy := image.NewGray(img.Bounds())
	draw.Draw(cpy, cpy.Bounds(), &img, image.Point{0, 0}, draw.Src)
	return *cpy
}
