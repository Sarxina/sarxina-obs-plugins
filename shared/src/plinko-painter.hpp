// How a Plinko board (plinko.hpp) looks in every plugin that has one: round pegs and thin dividers, in one colour,
// painted in the board's own units onto any QPainter.
#pragma once

#include "plinko.hpp"

class QColor;
class QPainter;

namespace sarxina {

void paintPlinko(QPainter &g, const Plinko &board, const QColor &color);

} // namespace sarxina
