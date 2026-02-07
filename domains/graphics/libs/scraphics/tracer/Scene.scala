package com.muchq.scraphics
package tracer

case class Scene(
    viewportSize: Double,
    projectionPlane: Double,
    backgroundColor: Color,
    spheres: List[Sphere],
    lights: List[Light]
)
