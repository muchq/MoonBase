package tracy

type Camera struct {
	Position Vec3
	Target   Vec3
	Up       Vec3
	Forward  Vec3
	Right    Vec3
	UpWorld  Vec3
}

func NewCamera(position, target, up Vec3) Camera {
	camera := Camera{
		Position: position,
		Target:   target,
		Up:       up,
	}
	camera.updateVectors()
	return camera
}

func NewCameraLookAt(position, target Vec3) Camera {
	return NewCamera(position, target, Vec3{0, 1, 0})
}

func (c *Camera) updateVectors() {
	c.Forward = c.Target.Sub(c.Position).Normalize()
	c.Right = c.Forward.Cross(c.Up).Normalize()
	c.UpWorld = c.Right.Cross(c.Forward).Normalize()
}

func (c *Camera) LookAt(target Vec3) {
	c.Target = target
	c.updateVectors()
}

func (c *Camera) Move(direction Vec3) {
	c.Position = c.Position.Add(direction)
	c.Target = c.Target.Add(direction)
}

func (c Camera) GetRayDirection(canvasPoint Vec2, image *Image, scene Scene) Vec3 {
	viewportX := canvasPoint.X * scene.ViewportSize / float64(image.Width)
	viewportY := canvasPoint.Y * scene.ViewportSize / float64(image.Height)

	localDirection := c.Right.Scale(viewportX).
		Add(c.UpWorld.Scale(viewportY)).
		Add(c.Forward.Scale(scene.ProjectionPlane))

	return localDirection.Normalize()
}
