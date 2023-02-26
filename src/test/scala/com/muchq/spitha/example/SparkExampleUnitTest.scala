package muchq.spitha.example

import com.muchq.spitha.example.SparkExample
import org.scalatest.funsuite.AnyFunSuite

class SparkExampleUnitTest extends AnyFunSuite {
  test("it should do stuff") {
    val sequenceSize = SparkExample.computeSequenceSize(10)
    assert(sequenceSize == 1_000_000, "slices")
  }

  test("it should not rollover") {
    val sequenceSize = SparkExample.computeSequenceSize(Int.MaxValue - 10)
    assert(sequenceSize == Int.MaxValue)
  }

  test("it should throw on negative slices") {
    assertThrows[AssertionError] {
      SparkExample.computeSequenceSize(-10)
    }
  }
}
