package tracy

type Image struct {
	Width  int
	Height int
	Data   [][]Color
}

func NewImage(width, height int) *Image {
	data := make([][]Color, width)
	for i := range data {
		data[i] = make([]Color, height)
	}
	return &Image{
		Width:  width,
		Height: height,
		Data:   data,
	}
}

func (img *Image) PutPixel(x, y float64, color Color) {
	px := int(float64(img.Width)/2 + x)
	py := int(float64(img.Height)/2-y) - 1
	if px >= 0 && px < img.Width && py >= 0 && py < img.Height {
		img.Data[px][py] = color
	}
}