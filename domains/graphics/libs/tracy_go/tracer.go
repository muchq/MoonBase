package tracy

import "math"

func DrawScene(scene Scene, image *Image, camera Camera) {
	for x := -image.Width / 2; x < image.Width/2; x++ {
		for y := -image.Height / 2; y < image.Height/2; y++ {
			direction := camera.GetRayDirection(Vec2{float64(x), float64(y)}, image, scene)
			color := traceRay(camera.Position, direction, 1.0, Inf, scene, 2)
			image.PutPixel(float64(x), float64(y), color)
		}
	}
}

func canvasToViewport(canvasPoint Vec2, image *Image, scene Scene) Vec3 {
	return Vec3{
		X: canvasPoint.X * scene.ViewportSize / float64(image.Width),
		Y: canvasPoint.Y * scene.ViewportSize / float64(image.Height),
		Z: scene.ProjectionPlane,
	}
}

func intersectRaySphere(origin, direction Vec3, sphere Sphere) (float64, float64) {
	originToSphere := origin.Sub(sphere.Center)
	
	a := direction.Dot(direction)
	b := originToSphere.Dot(direction) * 2
	c := originToSphere.Dot(originToSphere) - sphere.R2
	
	discriminant := b*b - 4*a*c
	if discriminant < 0 {
		return Inf, Inf
	}
	
	sqrtDiscr := math.Sqrt(discriminant)
	t1 := (-b - sqrtDiscr) / (2 * a)
	t2 := (-b + sqrtDiscr) / (2 * a)
	return t1, t2
}

func reflectRay(normal, ray Vec3) Vec3 {
	return normal.Scale(2 * normal.Dot(ray)).Sub(ray)
}

func closestIntersection(origin, direction Vec3, tMin, tMax float64, spheres []Sphere) (*Sphere, float64) {
	var closestSphere *Sphere
	closestT := Inf
	
	for i := range spheres {
		t1, t2 := intersectRaySphere(origin, direction, spheres[i])
		
		if t1 < closestT && tMin < t1 && t1 < tMax {
			closestT = t1
			closestSphere = &spheres[i]
		}
		
		if t2 < closestT && tMin < t2 && t2 < tMax {
			closestT = t2
			closestSphere = &spheres[i]
		}
	}
	
	return closestSphere, closestT
}

func anyIntersection(origin, direction Vec3, tMin, tMax float64, spheres []Sphere) bool {
	// Optimization: precompute 'a' which is constant for the ray
	a := direction.Dot(direction)
	inv2a := 1.0 / (2 * a)

	for i := range spheres {
		// Inline intersection logic to avoid function call overhead and reuse 'a'
		sphere := spheres[i]
		originToSphere := origin.Sub(sphere.Center)

		b := originToSphere.Dot(direction) * 2
		c := originToSphere.Dot(originToSphere) - sphere.R2

		discriminant := b*b - 4*a*c
		if discriminant < 0 {
			continue
		}

		sqrtDiscr := math.Sqrt(discriminant)
		t1 := (-b - sqrtDiscr) * inv2a

		if tMin < t1 && t1 < tMax {
			return true
		}

		t2 := (-b + sqrtDiscr) * inv2a
		if tMin < t2 && t2 < tMax {
			return true
		}
	}
	return false
}

func computeLighting(point, normal, view Vec3, scene Scene, specular float64) float64 {
	intensity := 0.0
	
	for _, light := range scene.Lights {
		if light.Type == Ambient {
			intensity += light.Intensity
		} else {
			var ray Vec3
			var tMax float64
			
			switch light.Type {
			case Point:
				ray = light.Position.Sub(point)
				tMax = 1.0
			case Directional:
				ray = light.Position
				tMax = Inf
			}
			
			if !anyIntersection(point, ray, Epsilon, tMax, scene.Spheres) {
				nDotR := normal.Dot(ray)
				if nDotR > 0 {
					intensity += light.Intensity * (nDotR / (normal.Length() * ray.Length()))
				}
				
				if specular > 0 {
					reflectedRay := reflectRay(normal, ray)
					rDotV := reflectedRay.Dot(view)
					if rDotV > 0 {
						intensity += light.Intensity * math.Pow(rDotV/(reflectedRay.Length()*view.Length()), specular)
					}
				}
			}
		}
	}
	
	return intensity
}

func traceRay(origin, direction Vec3, tMin, tMax float64, scene Scene, recursionDepth int) Color {
	closestSphere, closestT := closestIntersection(origin, direction, tMin, tMax, scene.Spheres)
	
	if closestSphere == nil {
		return Background
	}
	
	point := origin.Add(direction.Scale(closestT))
	n := point.Sub(closestSphere.Center)
	normal := n.Scale(1 / n.Length())
	
	view := direction.Scale(-1)
	lighting := computeLighting(point, normal, view, scene, closestSphere.Specular)
	localColor := closestSphere.Color.Scale(lighting)
	
	if recursionDepth <= 0 || closestSphere.Reflective <= 0 {
		return localColor
	}
	
	reflectedRay := reflectRay(normal, view)
	reflectedColor := traceRay(point, reflectedRay, Epsilon, Inf, scene, recursionDepth-1)
	
	return localColor.Scale(1 - closestSphere.Reflective).Add(reflectedColor.Scale(closestSphere.Reflective))
}