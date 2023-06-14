package com.muchq.scraphics.tracer

case class Image(width: Int, height: Int):
  val data: Array[Array[Color]] = Array.ofDim[Color](width, height)

  def putPixel(x: Double, y: Int, color: Color): Unit =
    val px: Int = (width / 2 + x).toInt
    val py: Int = (height / 2 - y).toInt - 1
    if 0 <= px && px < width && 0 <= py && py < height then
      data(px)(py) = color
