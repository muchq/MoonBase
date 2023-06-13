package lib

import (
	"image"
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
