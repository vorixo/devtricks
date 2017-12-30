package test.scala

import org.scalatest.FlatSpec
import org.scalatest.Matchers

import main.scala.Cell

class DefaultCellSpec extends FlatSpec with Matchers {
  behavior of "A Default Cell"
  
  it should "start empty" in {
    Cell.Default.isEmpty should be (true)
  }
}