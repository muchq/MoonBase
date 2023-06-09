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

	greyImage := images.BoxBlurX(imageData, 4, depth)
	_ = image_io.WriteImageAsPng(&greyImage, path+".grey.png")
}

func parseDepth() (int, error) {
	if len(os.Args) < 3 {
		return 3, nil // default to 3
	}
	return strconv.Atoi(os.Args[2])
}
