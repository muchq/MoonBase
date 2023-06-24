package com.muchq.spitha

import org.apache.spark.sql.SparkSession

abstract class SparkApp {
  def main(args: Array[String]): Unit = {
    val spark = SparkSession.builder
      .appName("Spark App")
      .config("spark.master", "spark://upstairs-mini.local:7077")
      .getOrCreate()

    try {
      run(args)(spark)
    } finally {
      spark.stop()
    }
  }

  def run(args: Array[String])(implicit spark: SparkSession): Unit = ???
}
