// The dock each plugin here puts in OBS in place of the Controls panel: the shared panel with the plugin's game on
// it. It feeds them the mouse, the keys and the time, keeps the panel in step with OBS, and remembers practice or
// real mode between sessions.
#pragma once

#include "control-panel.hpp"
#include "game.hpp"
#include "obs-bridge.hpp"
#include "panel-painter.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>

namespace sarxina {

class ControlsDock : public QWidget {
	Q_OBJECT

public:
	enum class Use {
		Dock,     // the real dock: follows OBS and keeps its settings
		SelfTest, // never shown: practice mode, its own clock, reads and writes no settings
	};

	explicit ControlsDock(std::unique_ptr<Game> game, Use use = Use::Dock, QWidget *parent = nullptr);
	~ControlsDock() override;

	QSize sizeHint() const override { return {640, 430}; }
	QSize minimumSizeHint() const override { return {240, 160}; }

	bool practice() const { return panel_.practice(); }

	// For the self-test, which runs the dock with nothing on screen.
	ControlPanel &panel() { return panel_; }
	double clock() const { return now(); }
	void setPanelSize(int width, int height);
	void advance(double seconds);
	QImage snapshot(qreal pixelRatio);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	static void frontendEvent(enum obs_frontend_event event, void *self);
	void onFrontendEvent(enum obs_frontend_event event);
	// In real mode, reads what OBS is doing now and shows it.
	void readObs();

	double now() const;
	void tick();
	void fitPanel();
	Point toPanel(const QPointF &position) const;

	void openMenu();
	void setPractice(bool practice);
	void loadSettings();
	void saveSettings() const;

	Use use_;
	std::unique_ptr<Game> game_; // before panel_, which plays it
	ControlPanel panel_;
	ObsBridge bridge_;
	PanelPainter painter_;

	QTimer timer_;
	QElapsedTimer elapsed_;
	double testClock_ = 0; // milliseconds; the self-test's own clock
	double last_ = 0;
	double pending_ = 0; // seconds of panel time not yet stepped
	bool listening_ = false;
};

} // namespace sarxina
