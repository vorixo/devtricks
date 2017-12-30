package test.scala

import org.scalatest.Finders
import org.scalatest.FlatSpec
import org.scalatest.Matchers

import main.scala.Board

class DefaultBoardSpec extends FlatSpec with Matchers {
  behavior of "A Default Board"
  
  it should "start empty" in {
    Board.Default.isEmpty should be (true)
  }
  
  it should "have 3 rows" in {
    Board.Default.rows should be (3)
  }
  
  it should "have 3 columns" in {
    Board.Default.columns should be (3)
  }
  
  it should "have 9 Cells" in {
    Board.Default.cellCount should be (9)
  }
}