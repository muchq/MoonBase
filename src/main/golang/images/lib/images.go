package lib

import (
	"image"
	_ "image/gif"
	_ "image/jpeg"
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
