#include "plinko-paint.hpp"

#include "panel-colors.hpp"
#include "sling-game.hpp"

#include <QPainter>
#include <QPolygonF>

#include <cmath>

using namespace sling;

void paintPlinko(QPainter &g, const Plinko &board)
{
	g.setPen(Qt::NoPen);
	g.setBrush(kGroundColor);
	for (int row = 1; row < board.rows(); row += 2) { // where the walls reach in to the end pegs
		const double y = board.rowY(row), rise = y - board.rowY(row - 1);
		for (const double wall : {0.0, kWidth}) {
			const double tip = std::abs(wall - board.pegX(row, 0));
			g.drawPolygon(QPolygonF({QPointF(wall, y - rise), QPointF(tip, y), QPointF(wall, y + rise)}));
		}
	}

	g.setBrush(kIdleColor);
	for (int row = 0; row < board.rows(); row++) {
		for (int peg = 0; peg < board.pegs(row); peg++)
			g.drawEllipse(QPointF(board.pegX(row, peg), board.rowY(row)), kPegRadius, kPegRadius);
	}

	g.setPen(QPen(kIdleColor, kDividerWidth, Qt::SolidLine, Qt::RoundCap));
	for (int slot = 1; slot < board.slotCount(); slot++) {
		const double x = board.slotWidth() * slot;
		g.drawLine(QPointF(x, board.dividerTop()), QPointF(x, board.ground() - kDividerWidth / 2));
	}
}
