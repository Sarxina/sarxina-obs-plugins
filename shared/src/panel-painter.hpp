// How the panel looks: OBS's dark Controls panel with the plugin's game on it, a status strip along the bottom like
// OBS's own, and the power-off gag over everything. It paints in panel units onto any QPainter, so the same code
// draws the dock and the self-test's pictures.
#pragma once

#include "control-panel.hpp"

#include <QFont>
#include <QImage>
#include <QString>

class QPainter;

namespace sarxina {

// The words for a locale key, in OBS's language.
QString text(const char *key);

class PanelPainter {
public:
	PanelPainter();

	// `scale` is device-independent pixels per panel unit, `left` how many of those pixels lie between the left edge
	// of what is painted and the panel's. The background, the floor, the status strip and the power-off reach across
	// them, on both sides.
	void paint(QPainter &g, const ControlPanel &panel, double now, double scale, double left);
	// Whether a point, in panel units, is on the gear that opens the menu.
	static bool overGear(Point p);

private:
	// A font and where its baseline sits below the middle of a line of text.
	struct Face {
		QFont font;
		double baseline = 0;
	};

	void paintButton(QPainter &g, const ControlPanel &panel, const Button &button, double deviceScale);
	void paintHead(QPainter &g, const Head &head, double deviceScale);
	void paintToast(QPainter &g, const ControlPanel &panel, double now);
	void paintStatus(QPainter &g, const ControlPanel &panel, double now, double side);
	void paintPowerOff(QPainter &g, const ControlPanel &panel, double now, double side) const;
	const QImage &buttonShadow(double buttonWidth, double deviceScale);
	const QImage &headAt(int devicePixels);

	Face label_, heading_, toast_, status_, gear_;
	double widestLabel_ = 0; // the longest text a button can show, in the label font
	QImage head_;            // round already
	QImage headScaled_;      // head_ at the size it is drawn
	QImage shadow_;          // the soft shadow under a button
	double shadowScale_ = 0;
	double shadowWidth_ = 0;
};

} // namespace sarxina
