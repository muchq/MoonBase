package tracy

import (
	"math"
	"testing"
)

func TestVec3Operations(t *testing.T) {
	v1 := NewVec3(1, 2, 3)
	v2 := NewVec3(4, 5, 6)

	// Test NewVec3
	if v1.X != 1 || v1.Y != 2 || v1.Z != 3 {
		t.Errorf("NewVec3 failed: expected (1,2,3), got (%f,%f,%f)", v1.X, v1.Y, v1.Z)
	}

	// Test Add
	sum := v1.Add(v2)
	expected := Vec3{5, 7, 9}
	if sum != expected {
		t.Errorf("Add failed: expected %v, got %v", expected, sum)
	}

	// Test Sub
	diff := v2.Sub(v1)
	expected = Vec3{3, 3, 3}
	if diff != expected {
		t.Errorf("Sub failed: expected %v, got %v", expected, diff)
	}

	// Test Scale
	scaled := v1.Scale(2)
	expected = Vec3{2, 4, 6}
	if scaled != expected {
		t.Errorf("Scale failed: expected %v, got %v", expected, scaled)
	}

	// Test Dot
	dot := v1.Dot(v2)
	expectedDot := 32.0 // 1*4 + 2*5 + 3*6
	if dot != expectedDot {
		t.Errorf("Dot failed: expected %f, got %f", expectedDot, dot)
	}

	// Test Length
	v := NewVec3(3, 4, 0)
	length := v.Length()
	expectedLength := 5.0
	if math.Abs(length-expectedLength) > 1e-10 {
		t.Errorf("Length failed: expected %f, got %f", expectedLength, length)
	}

	// Test Normalize
	normalized := v.Normalize()
	expectedNorm := NewVec3(0.6, 0.8, 0)
	if math.Abs(normalized.X-expectedNorm.X) > 1e-10 ||
		math.Abs(normalized.Y-expectedNorm.Y) > 1e-10 ||
		math.Abs(normalized.Z-expectedNorm.Z) > 1e-10 {
		t.Errorf("Normalize failed: expected %v, got %v", expectedNorm, normalized)
	}

	// Test Normalize of zero vector
	zero := NewVec3(0, 0, 0)
	normalizedZero := zero.Normalize()
	if normalizedZero != zero {
		t.Errorf("Normalize of zero vector failed: expected %v, got %v", zero, normalizedZero)
	}
}

func TestVec2Operations(t *testing.T) {
	v := NewVec2(3.5, -2.1)
	if v.X != 3.5 || v.Y != -2.1 {
		t.Errorf("NewVec2 failed: expected (3.5,-2.1), got (%f,%f)", v.X, v.Y)
	}
}

func TestColorOperations(t *testing.T) {
	c1 := NewColor(10, 20, 30)
	c2 := NewColor(5, 15, 25)

	// Test NewColor
	if c1.R != 10 || c1.G != 20 || c1.B != 30 {
		t.Errorf("NewColor failed: expected (10,20,30), got (%f,%f,%f)", c1.R, c1.G, c1.B)
	}

	// Test Scale
	scaled := c1.Scale(2)
	expected := Color{20, 40, 60}
	if scaled != expected {
		t.Errorf("Color Scale failed: expected %v, got %v", expected, scaled)
	}

	// Test Add
	sum := c1.Add(c2)
	expected = Color{15, 35, 55}
	if sum != expected {
		t.Errorf("Color Add failed: expected %v, got %v", expected, sum)
	}
}

func TestClampValue(t *testing.T) {
	tests := []struct {
		input    float64
		expected uint8
	}{
		{-10, 0},
		{0, 0},
		{127, 127},
		{255, 255},
		{300, 255},
	}

	for _, test := range tests {
		result := ClampValue(test.input)
		if result != test.expected {
			t.Errorf("ClampValue(%f) failed: expected %d, got %d", test.input, test.expected, result)
		}
	}
}

func TestSphereCreation(t *testing.T) {
	center := NewVec3(1, 2, 3)
	radius := 5.0
	color := Red
	specular := 500.0
	reflective := 0.3

	sphere := NewSphere(center, radius, color, specular, reflective)

	if sphere.Center != center {
		t.Errorf("Sphere center failed: expected %v, got %v", center, sphere.Center)
	}
	if sphere.Radius != radius {
		t.Errorf("Sphere radius failed: expected %f, got %f", radius, sphere.Radius)
	}
	if sphere.Color != color {
		t.Errorf("Sphere color failed: expected %v, got %v", color, sphere.Color)
	}
	if sphere.Specular != specular {
		t.Errorf("Sphere specular failed: expected %f, got %f", specular, sphere.Specular)
	}
	if sphere.Reflective != reflective {
		t.Errorf("Sphere reflective failed: expected %f, got %f", reflective, sphere.Reflective)
	}
	if sphere.R2 != radius*radius {
		t.Errorf("Sphere R2 failed: expected %f, got %f", radius*radius, sphere.R2)
	}
}

func TestLightCreation(t *testing.T) {
	position := NewVec3(10, 10, 10)
	intensity := 0.7
	
	light := NewLight(Point, intensity, position)
	
	if light.Type != Point {
		t.Errorf("Light type failed: expected %v, got %v", Point, light.Type)
	}
	if light.Intensity != intensity {
		t.Errorf("Light intensity failed: expected %f, got %f", intensity, light.Intensity)
	}
	if light.Position != position {
		t.Errorf("Light position failed: expected %v, got %v", position, light.Position)
	}
}

func TestSceneCreation(t *testing.T) {
	viewportSize := 1.0
	projectionPlane := 1.0
	backgroundColor := Black
	spheres := []Sphere{NewSphere(NewVec3(0, 0, 3), 1, Red, 500, 0.2)}
	lights := []Light{NewLight(Ambient, 0.2, NewVec3(0, 0, 0))}

	scene := NewScene(viewportSize, projectionPlane, backgroundColor, spheres, lights)

	if scene.ViewportSize != viewportSize {
		t.Errorf("Scene viewport size failed: expected %f, got %f", viewportSize, scene.ViewportSize)
	}
	if scene.ProjectionPlane != projectionPlane {
		t.Errorf("Scene projection plane failed: expected %f, got %f", projectionPlane, scene.ProjectionPlane)
	}
	if scene.BackgroundColor != backgroundColor {
		t.Errorf("Scene background color failed: expected %v, got %v", backgroundColor, scene.BackgroundColor)
	}
	if len(scene.Spheres) != 1 {
		t.Errorf("Scene spheres failed: expected 1 sphere, got %d", len(scene.Spheres))
	}
	if len(scene.Lights) != 1 {
		t.Errorf("Scene lights failed: expected 1 light, got %d", len(scene.Lights))
	}
}

func TestImageCreation(t *testing.T) {
	width, height := 100, 80
	img := NewImage(width, height)

	if img.Width != width {
		t.Errorf("Image width failed: expected %d, got %d", width, img.Width)
	}
	if img.Height != height {
		t.Errorf("Image height failed: expected %d, got %d", height, img.Height)
	}
	if len(img.Data) != width {
		t.Errorf("Image data width failed: expected %d, got %d", width, len(img.Data))
	}
	if len(img.Data[0]) != height {
		t.Errorf("Image data height failed: expected %d, got %d", height, len(img.Data[0]))
	}
}

func TestImagePutPixel(t *testing.T) {
	img := NewImage(10, 10)
	color := Red

	// Test putting pixel in valid range
	img.PutPixel(0, 0, color)
	px := int(float64(img.Width)/2 + 0)
	py := int(float64(img.Height)/2-0) - 1
	if img.Data[px][py] != color {
		t.Errorf("PutPixel failed: expected %v, got %v", color, img.Data[px][py])
	}

	// Test putting pixel outside range (should not crash)
	img.PutPixel(1000, 1000, color)
	img.PutPixel(-1000, -1000, color)
}

func TestIntersectRaySphere(t *testing.T) {
	// Test ray hitting sphere
	origin := NewVec3(0, 0, 0)
	direction := NewVec3(0, 0, 1)
	sphere := NewSphere(NewVec3(0, 0, 5), 1, Red, 500, 0)

	t1, t2 := intersectRaySphere(origin, direction, sphere)

	if t1 != 4.0 || t2 != 6.0 {
		t.Errorf("Ray-sphere intersection failed: expected (4,6), got (%f,%f)", t1, t2)
	}

	// Test ray missing sphere
	direction = NewVec3(1, 0, 0)
	t1, t2 = intersectRaySphere(origin, direction, sphere)

	if !math.IsInf(t1, 1) || !math.IsInf(t2, 1) {
		t.Errorf("Ray-sphere miss failed: expected (Inf,Inf), got (%f,%f)", t1, t2)
	}
}

func TestReflectRay(t *testing.T) {
	normal := NewVec3(0, 1, 0)  // pointing up
	ray := NewVec3(1, -1, 0)    // coming down and right

	reflected := reflectRay(normal, ray)
	// Formula: R = 2(N·L)N - L
	// N·L = 0*1 + 1*(-1) + 0*0 = -1
	// 2(N·L)N = 2*(-1)*(0,1,0) = (0,-2,0)
	// R = (0,-2,0) - (1,-1,0) = (-1,-1,0)
	expected := NewVec3(-1, -1, 0)

	if math.Abs(reflected.X-expected.X) > 1e-10 ||
		math.Abs(reflected.Y-expected.Y) > 1e-10 ||
		math.Abs(reflected.Z-expected.Z) > 1e-10 {
		t.Errorf("Reflect ray failed: expected %v, got %v", expected, reflected)
	}
}

func TestClosestIntersection(t *testing.T) {
	origin := NewVec3(0, 0, 0)
	direction := NewVec3(0, 0, 1)
	
	sphere1 := NewSphere(NewVec3(0, 0, 5), 1, Red, 500, 0)
	sphere2 := NewSphere(NewVec3(0, 0, 3), 0.5, Blue, 500, 0)
	spheres := []Sphere{sphere1, sphere2}

	closestSphere, closestT := closestIntersection(origin, direction, 0.001, Inf, spheres)

	// Should hit sphere2 first (at t=2.5)
	if closestSphere == nil {
		t.Error("No intersection found when one expected")
	} else if closestSphere.Color != Blue {
		t.Error("Wrong sphere returned - should be the blue one (closer)")
	}
	if math.Abs(closestT-2.5) > 1e-10 {
		t.Errorf("Wrong intersection distance: expected 2.5, got %f", closestT)
	}

	// Test no intersection
	direction = NewVec3(1, 0, 0)
	closestSphere, closestT = closestIntersection(origin, direction, 0.001, Inf, spheres)
	if closestSphere != nil {
		t.Error("Found intersection when none expected")
	}
	if !math.IsInf(closestT, 1) {
		t.Errorf("Expected infinite t when no intersection, got %f", closestT)
	}
}

func TestCanvasToViewport(t *testing.T) {
	image := NewImage(100, 100)
	scene := NewScene(1.0, 1.0, Black, nil, nil)

	canvasPoint := NewVec2(50, 50)
	viewport := canvasToViewport(canvasPoint, image, scene)

	expected := NewVec3(0.5, 0.5, 1.0)
	if math.Abs(viewport.X-expected.X) > 1e-10 ||
		math.Abs(viewport.Y-expected.Y) > 1e-10 ||
		math.Abs(viewport.Z-expected.Z) > 1e-10 {
		t.Errorf("Canvas to viewport failed: expected %v, got %v", expected, viewport)
	}
}

func TestComputeLighting(t *testing.T) {
	point := NewVec3(0, 0, 3)
	normal := NewVec3(0, 0, -1) // pointing toward camera
	view := NewVec3(0, 0, -1)   // looking at point

	lights := []Light{
		NewLight(Ambient, 0.2, NewVec3(0, 0, 0)),
		NewLight(Point, 0.6, NewVec3(2, 1, 0)),
	}
	
	scene := NewScene(1.0, 1.0, Black, nil, lights)
	
	intensity := computeLighting(point, normal, view, scene, 500)
	
	// Should at least have ambient lighting
	if intensity < 0.2 {
		t.Errorf("Lighting too low: expected at least 0.2, got %f", intensity)
	}
}

func TestTraceRayBasic(t *testing.T) {
	origin := NewVec3(0, 0, 0)
	direction := NewVec3(0, 0, 1)
	
	sphere := NewSphere(NewVec3(0, 0, 5), 1, Red, 500, 0)
	lights := []Light{NewLight(Ambient, 0.5, NewVec3(0, 0, 0))}
	scene := NewScene(1.0, 1.0, Black, []Sphere{sphere}, lights)

	color := traceRay(origin, direction, 0.001, Inf, scene, 1)

	// Should return some red color (not black background)
	if color.R <= 0 {
		t.Error("Expected red color from red sphere, got no red component")
	}
}

func TestTraceRayBackground(t *testing.T) {
	origin := NewVec3(0, 0, 0)
	direction := NewVec3(1, 0, 0) // pointing away from any spheres
	
	sphere := NewSphere(NewVec3(0, 0, 5), 1, Red, 500, 0)
	lights := []Light{NewLight(Ambient, 0.5, NewVec3(0, 0, 0))}
	scene := NewScene(1.0, 1.0, Black, []Sphere{sphere}, lights)

	color := traceRay(origin, direction, 0.001, Inf, scene, 1)

	// Should return background color
	if color != Background {
		t.Errorf("Expected background color %v, got %v", Background, color)
	}
}

func TestConstants(t *testing.T) {
	// Test predefined colors
	if Black != (Color{0, 0, 0}) {
		t.Errorf("Black constant wrong: expected (0,0,0), got %v", Black)
	}
	if Red != (Color{255, 0, 0}) {
		t.Errorf("Red constant wrong: expected (255,0,0), got %v", Red)
	}
	if Green != (Color{0, 255, 0}) {
		t.Errorf("Green constant wrong: expected (0,255,0), got %v", Green)
	}
	if Blue != (Color{0, 0, 255}) {
		t.Errorf("Blue constant wrong: expected (0,0,255), got %v", Blue)
	}
	if Yellow != (Color{255, 255, 0}) {
		t.Errorf("Yellow constant wrong: expected (255,255,0), got %v", Yellow)
	}
	if White != (Color{255, 255, 255}) {
		t.Errorf("White constant wrong: expected (255,255,255), got %v", White)
	}

	// Test special values
	if !math.IsInf(Inf, 1) {
		t.Error("Inf constant is not positive infinity")
	}
	if Epsilon != 0.0001 {
		t.Errorf("Epsilon constant wrong: expected 0.0001, got %f", Epsilon)
	}
	if Background != Black {
		t.Errorf("Background constant wrong: expected %v, got %v", Black, Background)
	}
}