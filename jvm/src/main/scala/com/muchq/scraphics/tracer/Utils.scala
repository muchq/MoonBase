package com.muchq.scraphics.tracer

object Utils {
  def clampValue(v: Double): Int = Math.min(255, Math.max(0, v).toInt)
}
