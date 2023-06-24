package com.muchq.spitha.example

import com.muchq.spitha.SparkApp
import org.apache.spark.sql.SparkSession
import org.slf4j.LoggerFactory

import scala.math.random

object SparkExample extends SparkApp {
  private val log = LoggerFactory.getLogger(SparkExample.getClass)

  override def run(args: Array[String])(implicit spark: SparkSession): Unit = {
    val slices = if (args.length > 0) args(0).toInt else 2
    val n      = computeSequenceSize(slices)
    val count = spark.sparkContext
      .parallelize(seq = 1 until n, slices)
      .map { _ =>
        val x = random * 2 - 1
        val y = random * 2 - 1
        if (x * x + y * y <= 1) 1 else 0
      }
      .reduce(_ + _)
    log.info(s"Pi is roughly ${4.0 * count / (n - 1)}")
  }

  def computeSequenceSize(argSlice: Int): Int = {
    assert(argSlice > 0, "slices must be positive")
    math.min(100_000L * argSlice, Int.MaxValue).toInt // avoid overflow
  }
}
