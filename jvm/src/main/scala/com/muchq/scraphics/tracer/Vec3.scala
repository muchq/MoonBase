package com.muchq.scraphics.tracer

case class Vec3(x: Double, y: Double, z: Double):
  def +(o: Vec3): Vec3 =
    Vec3(x + o.x, y + o.y, z + o.z)

  def -(o: Vec3): Vec3 =
    Vec3(x - o.x, y - o.y, z - o.z)

  def length(): Double =
    Math.sqrt(this dot this)

  def *(d: Double): Vec3 =
    Vec3(x * d, y * d, z * d)

  def dot(o: Vec3): Double =
    x * o.x + y * o.y + z * o.z
