#include "curve-editor.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace sarxina::eq {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

const QColor background(0x1b, 0x1c, 0x22);
const QColor gridLine(0x34, 0x36, 0x40);
const QColor gridMajor(0x4a, 0x4d, 0x5a);
const QColor labelColor(0x9a, 0x9e, 0xae);
const QColor curveColor(0x7c, 0xe0, 0x5a);
const QColor curveFill(0x7c, 0xe0, 0x5a, 40);

bool hasGain(int band)
{
	const Kind kind = kindOf(band);
	return kind != Kind::HighPass && kind != Kind::LowPass;
}

QString hzText(double hz)
{
	return hz >= 1000 ? QString::number(hz / 1000, 'g', 3) + "k" : QString::number(hz, 'g', 3);
}

} // namespace

QColor bandColor(int band)
{
	static const QColor colors[BandCount] = {
		QColor(0xb0, 0xb4, 0xc4), QColor(0xe8, 0x5d, 0x75), QColor(0xf2, 0x9e, 0x4c),
		QColor(0xf5, 0xd5, 0x47), QColor(0x6c, 0xd6, 0x8b), QColor(0x4c, 0xc9, 0xf0),
		QColor(0x5b, 0x8d, 0xf6), QColor(0xb1, 0x7c, 0xf2), QColor(0xb0, 0xb4, 0xc4),
	};
	return colors[band];
}

QString bandLabel(int band)
{
	static const char *const labels[BandCount] = {"HP", "L", "1", "2", "3", "4", "5", "H", "LP"};
	return QString::fromLatin1(labels[band]);
}

// ---- the graph ------------------------------------------------------------------------------------------------------

CurveGraph::CurveGraph(Settings &settings, double sampleRate, QWidget *parent)
	: QWidget(parent),
	  settings_(settings),
	  sampleRate_(sampleRate)
{
	setMouseTracking(true);
	setMinimumSize(520, 240);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

QRectF CurveGraph::plot() const
{
	return QRectF(44, 12, width() - 44 - 14, height() - 12 - 26);
}

double CurveGraph::xOf(double hz) const
{
	const QRectF p = plot();
	return p.left() + p.width() * std::log(hz / minHz) / std::log(maxHz / minHz);
}

double CurveGraph::hzAt(double x) const
{
	const QRectF p = plot();
	return std::clamp(minHz * std::exp((x - p.left()) / p.width() * std::log(maxHz / minHz)), minHz, maxHz);
}

double CurveGraph::yOf(double db) const
{
	const QRectF p = plot();
	return p.center().y() - db / range() * p.height() / 2;
}

double CurveGraph::dbAt(double y) const
{
	const QRectF p = plot();
	return std::clamp((p.center().y() - y) / (p.height() / 2) * range(), -range(), range());
}

QPointF CurveGraph::nodeOf(int band) const
{
	const BandSettings &b = settings_.bands[band];
	return {xOf(b.hz), yOf(hasGain(band) ? b.db : 0)};
}

int CurveGraph::bandAt(QPointF point) const
{
	int nearest = -1;
	double best = 12 * 12;
	for (int band = 0; band < BandCount; band++) {
		const QPointF d = nodeOf(band) - point;
		const double distance = d.x() * d.x() + d.y() * d.y();
		if (distance <= best) { // later bands win ties, as they are drawn on top
			best = distance;
			nearest = band;
		}
	}
	return nearest;
}

void CurveGraph::paintEvent(QPaintEvent *)
{
	QPainter g(this);
	g.setRenderHint(QPainter::Antialiasing);
	g.fillRect(rect(), background);
	const QRectF p = plot();

	// Frequency grid: every 1, 2 ... 9 of each decade, labelled at 1, 2 and 5.
	QFont small = font();
	small.setPointSizeF(8);
	g.setFont(small);
	for (double decade = 10; decade <= 10000; decade *= 10) {
		for (int n = 1; n <= 9; n++) {
			const double hz = decade * n;
			if (hz < minHz || hz > maxHz)
				continue;
			const double x = xOf(hz);
			g.setPen(QPen(n == 1 ? gridMajor : gridLine, 1));
			g.drawLine(QPointF(x, p.top()), QPointF(x, p.bottom()));
			if (n == 1 || n == 2 || n == 5) {
				g.setPen(labelColor);
				g.drawText(QRectF(x - 30, p.bottom() + 4, 60, 18), Qt::AlignHCenter | Qt::AlignTop, hzText(hz));
			}
		}
	}

	// Gain grid: every 3 dB on the 30 dB range, every 12 on the 96 dB range.
	const double step = settings_.wideRange ? 12 : 3;
	for (double db = -range(); db <= range() + 1e-9; db += step) {
		const double y = yOf(db);
		g.setPen(QPen(db == 0 ? gridMajor : gridLine, db == 0 ? 1.5 : 1));
		g.drawLine(QPointF(p.left(), y), QPointF(p.right(), y));
		g.setPen(labelColor);
		g.drawText(QRectF(0, y - 9, p.left() - 6, 18), Qt::AlignRight | Qt::AlignVCenter,
			   QString::number(db, 'f', 0));
	}

	// The summed response of every band that is on, and the master gain.
	const Design design = Design::of(settings_, sampleRate_);
	QPainterPath curve, fill;
	const int steps = std::max(2, static_cast<int>(p.width()));
	for (int i = 0; i <= steps; i++) {
		const double x = p.left() + p.width() * i / steps;
		const double y = std::clamp(yOf(design.db(hzAt(x), sampleRate_)), p.top() - 2, p.bottom() + 2);
		if (i == 0) {
			curve.moveTo(x, y);
			fill.moveTo(x, yOf(0));
		}
		curve.lineTo(x, y);
		fill.lineTo(x, y);
	}
	fill.lineTo(p.right(), yOf(0));
	fill.closeSubpath();
	g.save();
	g.setClipRect(p.adjusted(-1, -1, 1, 1));
	g.fillPath(fill, curveFill);
	g.setPen(QPen(curveColor, 2));
	g.drawPath(curve);
	g.restore();

	// The nodes, filled when their band is on, each with its band's name.
	QFont one = small, two = small;
	one.setBold(true);
	two.setBold(true);
	two.setPointSizeF(6.5);
	for (int band = 0; band < BandCount; band++) {
		const QPointF at = nodeOf(band);
		const QColor color = bandColor(band);
		const bool on = settings_.bands[band].on;
		const QString label = bandLabel(band);
		const double radius = (band == dragging_ || band == hovered_ ? 2 : 0) + (label.size() > 1 ? 9 : 7);
		g.setPen(QPen(on ? color.lighter(130) : color.darker(140), 2));
		g.setBrush(on ? color : QColor(background));
		g.drawEllipse(at, radius, radius);
		g.setFont(label.size() > 1 ? two : one);
		g.setPen(on ? QColor(0x14, 0x14, 0x18) : color);
		g.drawText(QRectF(at.x() - radius, at.y() - radius, 2 * radius, 2 * radius), Qt::AlignCenter, label);
	}
}

void CurveGraph::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;
	dragging_ = bandAt(event->position());
	if (dragging_ >= 0)
		mouseMoveEvent(event);
}

void CurveGraph::mouseMoveEvent(QMouseEvent *event)
{
	if (dragging_ < 0) {
		const int over = bandAt(event->position());
		if (over != hovered_) {
			hovered_ = over;
			setCursor(over >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
			update();
		}
		return;
	}
	BandSettings &b = settings_.bands[dragging_];
	b.on = true; // a band you drag is a band you want
	b.hz = std::round(hzAt(event->position().x()) * 10) / 10;
	if (hasGain(dragging_))
		b.db = std::round(dbAt(event->position().y()) * 10) / 10;
	update();
	emit edited();
}

void CurveGraph::mouseReleaseEvent(QMouseEvent *)
{
	dragging_ = -1;
	update();
}

// ---- the window -----------------------------------------------------------------------------------------------------

CurveEditor::CurveEditor(const Settings &settings, double sampleRate, std::function<void(const Settings &)> changed,
			 QWidget *parent)
	: QWidget(parent, Qt::Window),
	  settings_(settings),
	  changed_(std::move(changed))
{
	setWindowTitle(text("EditorTitle"));
	auto *layout = new QVBoxLayout(this);
	graph_ = new CurveGraph(settings_, sampleRate, this);
	layout->addWidget(graph_, 1);
	connect(graph_, &CurveGraph::edited, this, [this] {
		showNumbers();
		if (changed_)
			changed_(settings_);
	});

	auto *grid = new QGridLayout();
	grid->setHorizontalSpacing(8);
	grid->addWidget(new QLabel(text("On")), 1, 0);
	grid->addWidget(new QLabel(text("Frequency")), 2, 0);
	grid->addWidget(new QLabel(text("Gain")), 3, 0);
	qLabel_ = new QLabel();
	grid->addWidget(qLabel_, 4, 0);
	for (int band = 0; band < BandCount; band++)
		buildColumn(band, grid);
	layout->addLayout(grid);

	auto *bottom = new QHBoxLayout();
	bottom->addWidget(new QLabel(text("MasterGain")));
	gain_ = new QDoubleSpinBox();
	gain_->setRange(-maxDb, maxDb);
	gain_->setDecimals(2);
	gain_->setSingleStep(0.1);
	gain_->setSuffix(" dB");
	connect(gain_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		if (showing_)
			return;
		settings_.gainDb = v;
		edited();
	});
	bottom->addWidget(gain_);
	bottom->addSpacing(24);
	bottom->addWidget(new QLabel(text("Constant")));
	constant_ = new QComboBox();
	constant_->addItem(text("ConstantQ"));
	constant_->addItem(text("ConstantWidth"));
	connect(constant_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (showing_)
			return;
		// Switching keeps every curve where it is: a width w at f is a Q of f / w.
		const bool width = index == 1;
		for (int band = 0; band < BandCount; band++) {
			if (kindOf(band) != Kind::Bell)
				continue;
			BandSettings &b = settings_.bands[band];
			if (width)
				b.width = std::clamp(b.hz / b.q, minWidth, maxWidth);
			else
				b.q = std::clamp(b.hz / b.width, minQ, maxQ);
		}
		settings_.constantWidth = width;
		showNumbers();
		edited();
	});
	bottom->addWidget(constant_);
	bottom->addSpacing(24);
	bottom->addWidget(new QLabel(text("Range")));
	range_ = new QComboBox();
	range_->addItem(text("Range30"));
	range_->addItem(text("Range96"));
	connect(range_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (showing_)
			return;
		settings_.wideRange = index == 1;
		edited();
	});
	bottom->addWidget(range_);
	bottom->addStretch(1);
	layout->addLayout(bottom);

	showNumbers();
	resize(980, 560);
}

void CurveEditor::buildColumn(int band, QGridLayout *grid)
{
	Column &c = columns_[band];
	const int col = band + 1;
	grid->setColumnStretch(col, 1);
	auto number = [](double low, double high, int decimals, double step) {
		auto *box = new QDoubleSpinBox();
		box->setRange(low, high);
		box->setDecimals(decimals);
		box->setSingleStep(step);
		box->setButtonSymbols(QAbstractSpinBox::NoButtons);
		box->setAlignment(Qt::AlignCenter);
		box->setMinimumWidth(64);
		return box;
	};
	auto *title = new QLabel(bandLabel(band));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QString("color: %1; font-weight: bold;").arg(bandColor(band).name()));
	grid->addWidget(title, 0, col);

	c.on = new QCheckBox();
	grid->addWidget(c.on, 1, col, Qt::AlignHCenter);
	connect(c.on, &QCheckBox::toggled, this, [this, band](bool on) {
		if (showing_)
			return;
		settings_.bands[band].on = on;
		edited();
	});

	c.hz = number(minHz, maxHz, 1, 1);
	c.hz->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
	grid->addWidget(c.hz, 2, col);
	connect(c.hz, &QDoubleSpinBox::valueChanged, this, [this, band](double v) {
		if (showing_)
			return;
		settings_.bands[band].hz = v;
		edited();
	});

	const Kind kind = kindOf(band);
	if (kind == Kind::HighPass || kind == Kind::LowPass) {
		c.slope = new QComboBox();
		for (int slope = 6; slope <= 48; slope += 6)
			c.slope->addItem(QString::number(slope) + " " + text("PerOctave"), slope);
		grid->addWidget(c.slope, 3, col);
		connect(c.slope, &QComboBox::currentIndexChanged, this, [this, band](int index) {
			if (showing_)
				return;
			settings_.bands[band].slope = 6 * (index + 1);
			edited();
		});
		return;
	}

	c.db = number(-maxDb, maxDb, 2, 0.1);
	grid->addWidget(c.db, 3, col);
	connect(c.db, &QDoubleSpinBox::valueChanged, this, [this, band](double v) {
		if (showing_)
			return;
		settings_.bands[band].db = v;
		edited();
	});

	if (kind == Kind::Bell) {
		c.q = number(minQ, maxQ, 2, 0.1);
		grid->addWidget(c.q, 4, col);
		connect(c.q, &QDoubleSpinBox::valueChanged, this, [this, band](double v) {
			if (showing_)
				return;
			if (settings_.constantWidth)
				settings_.bands[band].width = v;
			else
				settings_.bands[band].q = v;
			edited();
		});
	} else {
		c.order = new QComboBox();
		c.order->addItem(text("FirstOrder"));
		c.order->addItem(text("SecondOrder"));
		grid->addWidget(c.order, 4, col);
		connect(c.order, &QComboBox::currentIndexChanged, this, [this, band](int index) {
			if (showing_)
				return;
			settings_.bands[band].steep = index == 1;
			edited();
		});
	}
}

void CurveEditor::showNumbers()
{
	showing_ = true;
	for (int band = 0; band < BandCount; band++) {
		const Column &c = columns_[band];
		const BandSettings &b = settings_.bands[band];
		c.on->setChecked(b.on);
		c.hz->setValue(b.hz);
		if (c.db)
			c.db->setValue(b.db);
		if (c.q) {
			if (settings_.constantWidth) {
				c.q->setRange(minWidth, maxWidth);
				c.q->setDecimals(1);
				c.q->setSingleStep(1);
				c.q->setValue(b.width);
			} else {
				c.q->setRange(minQ, maxQ);
				c.q->setDecimals(2);
				c.q->setSingleStep(0.1);
				c.q->setValue(b.q);
			}
		}
		if (c.slope)
			c.slope->setCurrentIndex(std::clamp(b.slope / 6 - 1, 0, 7));
		if (c.order)
			c.order->setCurrentIndex(b.steep ? 1 : 0);
	}
	gain_->setValue(settings_.gainDb);
	constant_->setCurrentIndex(settings_.constantWidth ? 1 : 0);
	range_->setCurrentIndex(settings_.wideRange ? 1 : 0);
	qLabel_->setText(text(settings_.constantWidth ? "Width" : "Q"));
	showing_ = false;
}

void CurveEditor::edited()
{
	if (showing_)
		return;
	graph_->update();
	if (changed_)
		changed_(settings_);
}

void CurveEditor::setSettings(const Settings &settings)
{
	settings_ = settings;
	showNumbers();
	graph_->update();
}

} // namespace sarxina::eq
