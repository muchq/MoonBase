package lib

import (
	"image"
	"os"
)

func ReadImage(path string) (image.Image, string, error) {
	existingImageFile, err := os.Open(path)
	if err != nil {
		return nil, "", err
	}
	defer existingImageFile.Close()

	// Calling the generic image.Decode() will tell give us the data
	// and type of image it is as a string. We expect "png"
	return image.Decode(existingImageFile)
}
