package main.scala

object Cell {
  val Default = Cell() 
}

case class Cell() {
  def isEmpty: Boolean = true
}