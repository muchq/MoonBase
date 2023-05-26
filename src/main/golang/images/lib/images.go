package lib

import (
	"image"
	"image/png"
	"os"
)

func ReadImage(path string) (image.Image, error) {
	existingImageFile, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer existingImageFile.Close()
	return png.Decode(existingImageFile)
}
