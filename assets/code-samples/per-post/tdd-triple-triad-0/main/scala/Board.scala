package main.scala

object Board {
  val Default = Board(3, 3)
}

case class Board(rows: Int, columns: Int) {
  private val cells: List[Cell] = List.fill(rows * columns)(Cell.Default)
  
  lazy val cellCount: Int = cells.size
  def isEmpty: Boolean = cells.forall(_.isEmpty)
}