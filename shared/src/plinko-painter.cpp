#include "plinko-painter.hpp"

#include <QPainter>

namespace sarxina {

void paintPlinko(QPainter &g, const Plinko &board, const QColor &color)
{
	const PlinkoShape &shape = board.shape();
	g.setPen(Qt::NoPen);
	g.setBrush(color);
	for (int row = 0; row < shape.rows; row++) {
		for (int i = 0; i <= row; i++) {
			const Point p = board.peg(row, i);
			g.drawEllipse(QPointF(p.x, p.y), shape.pegRadius, shape.pegRadius);
		}
	}

	g.setPen(QPen(color, shape.dividerWidth, Qt::SolidLine, Qt::RoundCap));
	const double top = board.dividerTop();
	for (int slot = 1; slot < shape.slotCount; slot++) {
		const double x = board.slotWidth() * slot;
		g.drawLine(QPointF(x, top), QPointF(x, board.ground() - shape.dividerWidth / 2));
	}
}

} // namespace sarxina
