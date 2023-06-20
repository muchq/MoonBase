package com.muchq.scraphics

import com.muchq.scraphics.tracer.{Tracer, Color, Image, Light, LightType, Scene, Sphere, Vec2, Vec3}
import com.muchq.scraphics.tracer.Constants.{RED, YELLOW, BLUE, GREEN, BACKGROUND}
import com.muchq.scraphics.tracer.Utils.clampValue

import java.awt.image.BufferedImage
import javax.imageio.ImageIO
import java.io.File

def printImage(image: Image): Unit =
  def clamp(c: Color): Int =
    new java.awt.Color(clampValue(c.r), clampValue(c.g), clampValue(c.b)).getRGB

  val bufferedImage = new BufferedImage(image.width, image.height, BufferedImage.TYPE_3BYTE_BGR)
  for
    x <- 0 until image.width
    y <- 0 until image.height
  do
    bufferedImage.setRGB(x, y, clamp(image.data(x)(y)))

  ImageIO.write(bufferedImage, "png", new File("tracer_output.png"))


object App {
  def main(args: Array[String]): Unit =
    val viewportSize: Double = 1.0
    val projectionPlane: Double = 1.0
    val cameraPosition: Vec3 = Vec3(0, 0, -5)

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
    Tracer.drawScene(scene, image, cameraPosition)
    printImage(image)
}
