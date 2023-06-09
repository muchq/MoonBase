package main

import (
	"fmt"
	image_io "github.com/muchq/moonbase/golang/image_io"
	images "github.com/muchq/moonbase/golang/images"
	"os"
	"strconv"
)

func main() {
	path := os.Args[1]
	imageData, _, err := image_io.ReadImage(path)
	if err != nil {
		fmt.Println(err)
		return
	}

	depth, err := parseDepth()
	if err != nil {
		fmt.Println("invalid depth", err)
		return
	}

	_ = image_io.WriteImageAsPng(imageData, path+".png")

	greyBlurXImage := images.BoxBlurX(imageData, 1, depth)
	_ = image_io.WriteImageAsPng(&greyBlurXImage, path+".grey.X.png")

	greyBlurYImage := images.BoxBlurY(imageData, 1, depth)
	_ = image_io.WriteImageAsPng(&greyBlurYImage, path+".grey.Y.png")

	greyBlurBoxImage := images.BoxBlur(imageData, 1, depth)
	_ = image_io.WriteImageAsPng(&greyBlurBoxImage, path+".grey.Box.png")
}

func parseDepth() (int, error) {
	if len(os.Args) < 3 {
		return 3, nil // default to 3
	}
	return strconv.Atoi(os.Args[2])
}
