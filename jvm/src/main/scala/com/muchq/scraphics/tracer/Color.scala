package com.muchq.scraphics.tracer

case class Color(r: Double, g: Double, b: Double):
  def *(d: Double): Color =
    Color(r * d, g * d, b * d)

  def +(o: Color): Color =
    Color(r + o.r, g + o.g, b + o.b)
