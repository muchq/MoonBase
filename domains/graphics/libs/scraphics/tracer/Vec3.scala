package com.muchq.scraphics
package tracer

case class Vec3(x: Double, y: Double, z: Double):
  infix def +(o: Vec3): Vec3 =
    Vec3(x + o.x, y + o.y, z + o.z)

  infix def -(o: Vec3): Vec3 =
    Vec3(x - o.x, y - o.y, z - o.z)

  def length(): Double =
    Math.sqrt(this dot this)

  infix def *(d: Double): Vec3 =
    Vec3(x * d, y * d, z * d)

  infix def dot(o: Vec3): Double =
    x * o.x + y * o.y + z * o.z
