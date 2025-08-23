package main

import (
	"image"
	"image/color"
	"image/png"
	"log"
	"os"

	. "github.com/muchq/moonbase/go/tracy"
)

func main() {
	viewportSize := 1.0
	projectionPlane := 1.0
	cameraPosition := Vec3{0, 0, -5}

	spheres := []Sphere{
		NewSphere(Vec3{0, -1, 3}, 1, Red, 500, 0.2),
		NewSphere(Vec3{2, 0, 4}, 1, Blue, 500, 0.3),
		NewSphere(Vec3{-2, 0, 4}, 1, Green, 10, 0.4),
		NewSphere(Vec3{0, -5001, 0}, 5000, Yellow, 1000, 0.6),
	}

	lights := []Light{
		NewLight(Ambient, 0.2, Vec3{0, 0, 0}),
		NewLight(Point, 0.6, Vec3{2, 1, 0}),
		NewLight(Directional, 0.2, Vec3{1, 4, 4}),
	}

	scene := NewScene(viewportSize, projectionPlane, Background, spheres, lights)
	img := NewImage(600, 600)

	DrawScene(scene, img, cameraPosition)

	outputPath := "tracer_output.png"
	saveImage(img, outputPath)

	// Also print the absolute path
	absPath, _ := os.Getwd()
	log.Printf("Ray tracing completed. Output saved to %s/%s", absPath, outputPath)
}

func saveImage(img *Image, filename string) {
	output := image.NewRGBA(image.Rect(0, 0, img.Width, img.Height))

	for x := 0; x < img.Width; x++ {
		for y := 0; y < img.Height; y++ {
			c := img.Data[x][y]
			output.Set(x, y, color.RGBA{
				R: ClampValue(c.R),
				G: ClampValue(c.G),
				B: ClampValue(c.B),
				A: 255,
			})
		}
	}

	file, err := os.Create(filename)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()

	if err := png.Encode(file, output); err != nil {
		log.Fatal(err)
	}
}
