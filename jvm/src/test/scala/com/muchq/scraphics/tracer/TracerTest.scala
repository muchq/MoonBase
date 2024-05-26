package com.muchq.scraphics
package tracer

import org.scalatest.funsuite.AnyFunSuite

class TracerTest extends AnyFunSuite {

  test("it reflects rays") {
    val normal = Vec3(1.0, 0.0, 1.0)
    val ray = Vec3(2.5, 1.5, 3.5)

    val reflection = Tracer.reflectRay(normal, ray)

    assert(reflection == Vec3(9.5, -1.5, 8.5))
  }
}
