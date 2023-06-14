package com.muchq.scraphics.tracer

import com.muchq.scraphics.tracer.Constants.{INF, EPSILON, BACKGROUND}

object Tracer {
  def drawScene(scene: Scene, image: Image, cameraPosition: Vec3): Unit =
    for
      x <- (-image.width / 2).toInt until (image.width / 2).toInt
      y <- (-image.height / 2).toInt until (image.height / 2).toInt
    do
      val direction: Vec3 = canvasToViewport(Vec2(x, y), image, scene)
      val color: Color = traceRay(cameraPosition, direction, 1.0, INF, scene, 2)
      image.putPixel(x, y, color)

  private def canvasToViewport(canvasPoint: Vec2, image: Image, scene: Scene): Vec3 =
    Vec3(canvasPoint._1 * scene.viewportSize / image.width, canvasPoint._2 * scene.viewportSize / image.height, scene.projectionPlane)

  private def intersectRaySphere(origin: Vec3, direction: Vec3, sphere: Sphere): (Double, Double) =
    val originToSphere: Vec3 = origin - sphere.center

    val a = direction dot direction
    val b = (originToSphere dot direction) * 2
    val c = (originToSphere dot originToSphere) - sphere.r2

    val discriminant: Double = b*b - (4 * a * c)
    if discriminant < 0 then
      return (INF, INF)

    val sqrtDiscr: Double = Math.sqrt(discriminant)
    val t1: Double = (-b + sqrtDiscr) / (2 * a)
    val t2: Double = (-b - sqrtDiscr) / (2 * a)
    (t1, t2)

  private def reflectRay(normal: Vec3, ray: Vec3): Vec3 =
    (normal * 2 * (normal dot ray)) - ray

  private def closestIntersection(origin: Vec3, direction: Vec3, tMin: Double, tMax: Double, spheres: List[Sphere]): (Option[Sphere], Double) =
    def intersectHelper(s: Sphere): (Option[Sphere], Double) =
      val (t1, t2) = intersectRaySphere(origin, direction, s)
      var closestSphere: Option[Sphere] = None
      var closestT: Double = INF
      if t1 < closestT && tMin < t1 && t1 < tMax then
        closestT = t1
        closestSphere = Some(s)

      if t2 < closestT && tMin < t2 && t2 < tMax then
        closestT = t2
        closestSphere = Some(s)
      (closestSphere, closestT)

    spheres.map(s => intersectHelper(s)).minBy(p => p._2)

  private def specularLightIntensity(normal: Vec3, ray: Vec3, view: Vec3, light: Light, specular: Double): Double =
    if specular <= 0 then
      return 0.0

    val reflectedRay: Vec3 = reflectRay(normal, ray)
    val rDotV: Double = reflectedRay dot view
    if rDotV > 0 then
      light.intensity * Math.pow(rDotV / reflectedRay.length() * view.length(), specular)
    else 0.0

  private def computeLighting(point: Vec3, normal: Vec3, view: Vec3, scene: Scene, specular: Double): Double =
    var intensity = 0.0
    for
      light <- scene.lights
    do
      if light.lightType == LightType.AMBIENT then
        intensity += light.intensity
      else
        val (ray, tMax) = light.lightType match
          case LightType.POINT => (light.position - point, 1.0)
          case LightType.DIRECTIONAL => (light.position, INF)
          case _ => ???

        val (shadowSphereMaybe, _) = closestIntersection(point, ray, EPSILON, tMax, scene.spheres)
        if shadowSphereMaybe.isEmpty then
          val nDotR = normal dot ray
          if nDotR > 0 then
            intensity += (light.intensity * (nDotR / (normal.length() * ray.length())))

          if specular > 0 then
            val reflectedRay = reflectRay(normal, ray)
            val rDotV = reflectedRay dot view
            if rDotV > 0 then
              intensity += (light.intensity * Math.pow(rDotV / (reflectedRay.length() * view.length()), specular))

    intensity

  private def traceRay(origin: Vec3, direction: Vec3, tMin: Double, tMax: Double, scene: Scene, recursionDepth: Int = 0): Color =
    val (closestSphereMaybe, closestT) = closestIntersection(origin, direction, tMin, tMax, scene.spheres)
    closestSphereMaybe match
      case None => BACKGROUND
      case Some(s) =>
        val point      = origin + (direction * closestT)
        val n          = point - s.center
        val normal     = n * (1 / n.length())

        val view       = direction * -1
        val lighting   = computeLighting(point, normal, view, scene, s.specular)
        val localColor = s.color * lighting

        if recursionDepth <= 0 || s.reflective <= 0 then
          return localColor

        val reflectedRay: Vec3 = reflectRay(normal, view)
        val reflectedColor: Color = traceRay(point, reflectedRay, EPSILON, INF, scene, recursionDepth - 1)
        localColor * (1.0 - s.reflective) + reflectedColor * s.reflective
}
