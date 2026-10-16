// The equalizer's window, laid out like Audition's Parametric Equalizer: the frequency response on a log frequency
// axis and a dB grid, one node per band to drag (left and right for frequency, up and down for gain), and below it a
// column of numbers per band. Every change redraws the curve and goes to the filter at once.
#pragma once

#include "eq-design.hpp"

#include <QWidget>

#include <array>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;

namespace sarxina::eq {

// The graph on its own: grid, summed response and nodes.
class CurveGraph : public QWidget {
	Q_OBJECT

public:
	CurveGraph(Settings &settings, double sampleRate, QWidget *parent = nullptr);
	QSize sizeHint() const override { return {860, 340}; }

signals:
	void edited(); // a node was dragged

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	QRectF plot() const;
	double xOf(double hz) const;
	double hzAt(double x) const;
	double yOf(double db) const;
	double dbAt(double y) const;
	double range() const { return settings_.wideRange ? 48 : 15; }
	QPointF nodeOf(int band) const;
	int bandAt(QPointF point) const; // the nearest node within reach, or -1

	Settings &settings_;
	double sampleRate_;
	int dragging_ = -1;
	int hovered_ = -1;
};

// The whole window: the graph and the numbers under it.
class CurveEditor : public QWidget {
	Q_OBJECT

public:
	// `changed` gets the settings after every edit.
	CurveEditor(const Settings &settings, double sampleRate, std::function<void(const Settings &)> changed,
		    QWidget *parent = nullptr);

	// Shows settings that changed somewhere else (OBS's own undo, a websocket request).
	void setSettings(const Settings &settings);
	const Settings &settings() const { return settings_; }

private:
	struct Column {
		QCheckBox *on = nullptr;
		QDoubleSpinBox *hz = nullptr;
		QDoubleSpinBox *db = nullptr;  // bells and shelves
		QDoubleSpinBox *q = nullptr;   // bells
		QComboBox *slope = nullptr;    // pass filters
		QComboBox *order = nullptr;    // shelves
	};

	void buildColumn(int band, QGridLayout *grid);
	void showNumbers(); // copies settings_ into the boxes without echoing back
	void edited();      // the settings changed: redraw and tell the filter

	Settings settings_;
	std::function<void(const Settings &)> changed_;
	CurveGraph *graph_ = nullptr;
	std::array<Column, BandCount> columns_;
	QDoubleSpinBox *gain_ = nullptr;
	QComboBox *range_ = nullptr;
	bool showing_ = false;
};

// Each band's colour and its short name over its column ("HP", "L", "1" to "5", "H", "LP").
QColor bandColor(int band);
QString bandLabel(int band);

} // namespace sarxina::eq
