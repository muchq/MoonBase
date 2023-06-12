package lib

import (
	"fmt"
	"testing"
)

func TestReadImage(t *testing.T) {
	_, imageType, err := ReadImage("/static_content/marbles.png")
	if err != nil {
		fmt.Println("whyyyyyyyyyyyy")
	} else {
		fmt.Print(imageType)
	}
}
