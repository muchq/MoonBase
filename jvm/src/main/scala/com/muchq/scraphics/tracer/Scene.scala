package com.muchq.scraphics.tracer

case class Scene(
    viewportSize: Double,
    projectionPlane: Double,
    backgroundColor: Color,
    spheres: List[Sphere],
    lights: List[Light]
)
