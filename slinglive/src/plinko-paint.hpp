// Draws the Plinko board: the pegs, the walls and the dividers between the slots. The buttons in the slots and the
// head are the dock's to draw, as in the other play styles.
#pragma once

class QPainter;

namespace sling {
class Plinko;
}

void paintPlinko(QPainter &g, const sling::Plinko &board);
