package main

import (
	"fmt"
	images "github.com/muchq/moonbase/golang/images"
	"os"
)

func main() {
	var path = os.Args[1]
	imageData, err := images.ReadImage(path)
	if err != nil {
		fmt.Println(err)
		return
	}

	fmt.Println(imageData.Bounds())
}
