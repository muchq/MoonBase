import java.awt.image.BufferedImage
import javax.imageio.ImageIO
import java.io.File

val BLACK      = Color(0, 0, 0)
val RED        = Color(255, 0, 0)
val GREEN      = Color(0, 255, 0)
val BLUE       = Color(0, 0, 255)
val YELLOW     = Color(255, 255, 0)
val WHITE      = Color(255, 255, 255)
val BACKGROUND = BLACK

val INF        = Double.PositiveInfinity
val EPSILON    = 0.0001

case class Image(width: Int, height: Int):
  val data = Array.ofDim[Color](width, height)

  def putPixel(x: Double, y: Int, color: Color): Unit =
    val px: Int = (width / 2 + x).toInt
    val py: Int = (height / 2 - y).toInt - 1
    if 0 <= px && px < width && 0 <= py && py < height then
      data(px)(py) = color

def clampValue(v: Double): Int = Math.min(255, Math.max(0, v).toInt)

case class Color(r: Double, g: Double, b: Double):
  def *(d: Double): Color =
    Color(r*d, g*d, b*d)

  def +(o: Color): Color =
    Color(r + o.r, g + o.g, b + o.b)

case class Vec3(x: Double, y: Double, z: Double):
  def +(o: Vec3): Vec3 =
    Vec3(x + o.x, y + o.y, z + o.z)

  def -(o: Vec3): Vec3 =
    Vec3(x - o.x, y - o.y, z - o.z)

  def length(): Double =
    Math.sqrt(this dot this)

  def *(d: Double): Vec3 =
    Vec3(x*d, y*d, z*d)

  def dot(o: Vec3): Double =
    x*o.x + y*o.y + z*o.z

case class Vec2(x: Double, y: Double)

case class Sphere(center: Vec3, radius: Double, color: Color, specular: Double, reflective: Double):
  val r2: Double = radius*radius

enum LightType:
  case AMBIENT, POINT, DIRECTIONAL

case class Light(lightType: LightType, intensity: Double, position: Vec3)

case class Scene(viewportSize: Double, projectionPlane: Double, backgroundColor: Color, spheres: List[Sphere], lights: List[Light])

def canvasToViewport(canvasPoint: Vec2, image: Image, scene: Scene): Vec3 =
  Vec3(canvasPoint._1 * scene.viewportSize / image.width, canvasPoint._2 * scene.viewportSize / image.height, scene.projectionPlane)

def intersectRaySphere(origin: Vec3, direction: Vec3, sphere: Sphere): (Double, Double) =
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

def reflectRay(normal: Vec3, ray: Vec3): Vec3 =
  (normal * 2 * (normal dot ray)) - ray

def closestIntersection(origin: Vec3, direction: Vec3, tMin: Double, tMax: Double, spheres: List[Sphere]): (Option[Sphere], Double) =
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

def specularLightIntensity(normal: Vec3, ray: Vec3, view: Vec3, light: Light, specular: Double): Double =
  if specular <= 0 then
    return 0.0

  val reflectedRay: Vec3 = reflectRay(normal, ray)
  val rDotV: Double = reflectedRay dot view
  if rDotV > 0 then
    light.intensity * Math.pow(rDotV / reflectedRay.length() * view.length(), specular)
  else 0.0

def computeLighting(point: Vec3, normal: Vec3, view: Vec3, scene: Scene, specular: Double): Double =
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

def traceRay(origin: Vec3, direction: Vec3, tMin: Double, tMax: Double, scene: Scene, recursionDepth: Int = 0): Color =
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

def drawScene(scene: Scene, image: Image, cameraPosition: Vec3): Unit =
  for
    x <- (-image.width/2).toInt to (image.width / 2).toInt-1
    y <- (-image.height/2).toInt to (image.height / 2).toInt-1
  do
    val direction: Vec3 = canvasToViewport(Vec2(x, y), image, scene)
    val color: Color = traceRay(cameraPosition, direction, 1.0, INF, scene, 2)
    image.putPixel(x, y, color)

def printImage(image: Image): Unit =
  def clamp(c: Color): Int =
    new java.awt.Color(clampValue(c.r), clampValue(c.g), clampValue(c.b)).getRGB

  val bufferedImage = new BufferedImage(image.width, image.height, BufferedImage.TYPE_3BYTE_BGR)
  for
    x <- 0 to image.width-1
    y <- 0 to image.height-1
  do
    bufferedImage.setRGB(x, y, clamp(image.data(x)(y)))

  ImageIO.write(bufferedImage, "png", new File("tracer_output.png"))


@main def hello: Unit =
  val viewportSize: Double = 1.0
  val projectionPlane: Double = 1.0
  val cameraPosition: Vec3 = Vec3(0, 0, 0)

  val spheres: List[Sphere] = List(
    Sphere(Vec3(0, -1, 3), 1, RED, 500, 0.2),
    Sphere(Vec3(2, 0, 4), 1, BLUE, 500, 0.3),
    Sphere(Vec3(-2, 0, 4), 1, GREEN, 10, 0.4),
    Sphere(Vec3(0, -5001, 0), 5000, YELLOW, 1000, 0.6)
  )

  val lights: List[Light] = List(
    Light(LightType.AMBIENT, 0.2, Vec3(0, 0, 0)),
    Light(LightType.POINT, 0.6, Vec3(2, 1, 0)),
    Light(LightType.DIRECTIONAL, 0.2, Vec3(1, 4, 4))
  )

  val scene: Scene = Scene(viewportSize, projectionPlane, BACKGROUND, spheres, lights)
  val image = Image(600, 600)
  drawScene(scene, image, cameraPosition)
  printImage(image)
