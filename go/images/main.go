package main

import (
	"fmt"
	"os"
	"strconv"

	images "github.com/muchq/moonbase/go/images/lib"
)

func main() {
	path := os.Args[1]
	imageData, _, err := images.ReadImage(path)
	if err != nil {
		fmt.Println(err)
		return
	}

	depth, err := parseDepth()
	if err != nil {
		fmt.Println("invalid depth", err)
		return
	}

	_ = images.WriteImageAsPng(imageData, path+".png")

	greyBlurXImage := images.BoxBlurX(imageData, 1, depth)
	_ = images.WriteImageAsPng(&greyBlurXImage, path+".grey.X.png")

	greyBlurYImage := images.BoxBlurY(imageData, 1, depth)
	_ = images.WriteImageAsPng(&greyBlurYImage, path+".grey.Y.png")

	greyBlurBoxImage := images.BoxBlur(imageData, 1, depth)
	_ = images.WriteImageAsPng(&greyBlurBoxImage, path+".grey.Box.png")
}

func parseDepth() (int, error) {
	if len(os.Args) < 3 {
		return 3, nil // default to 3
	}
	return strconv.Atoi(os.Args[2])
}
