// The SlingLive dock: draws the game, feeds it the mouse and the keys, and keeps it in step with OBS.
#pragma once

#include "obs-bridge.hpp"
#include "sling-game.hpp"

#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QTimer>
#include <QWidget>

class QPainter;

class SlingDock : public QWidget {
	Q_OBJECT

public:
	enum class Use {
		Dock,     // the real dock: follows OBS and keeps its settings
		SelfTest, // never shown: practice mode, its own clock, reads and writes no settings
	};

	explicit SlingDock(Use use = Use::Dock, QWidget *parent = nullptr);
	~SlingDock() override;

	QSize sizeHint() const override { return {640, 430}; }
	QSize minimumSizeHint() const override { return {240, 160}; }

	bool practice() const { return game_.practice(); }

	// For the self-test, which runs the dock with nothing on screen.
	sling::Game &game() { return game_; }
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
	// A font and where its baseline sits below the middle of a line of text.
	struct Face {
		QFont font;
		double baseline = 0;
	};

	static void frontendEvent(enum obs_frontend_event event, void *self);
	void onFrontendEvent(enum obs_frontend_event event);

	double now() const;
	void tick();
	void fitPanel();
	QPointF toPanel(const QPointF &position) const;

	void paintPanel(QPainter &g, double now);
	void paintButton(QPainter &g, const sling::Button &button, double deviceScale);
	void paintPowerOff(QPainter &g, double now);
	const QImage &buttonShadow(double deviceScale);
	const QImage &headAt(int devicePixels);

	void openMenu();
	void setPractice(bool practice);
	void chooseHead();
	void resetHead();
	void loadHead();
	void setHead(const QImage &picture);
	void loadSettings();
	void saveSettings() const;

	Use use_;
	sling::Game game_;
	ObsBridge bridge_;

	QTimer timer_;
	QElapsedTimer elapsed_;
	double testClock_ = 0; // milliseconds; the self-test's own clock
	double last_ = 0;
	double pending_ = 0;   // seconds of game time not yet stepped
	bool listening_ = false;

	Face label_, heading_, toast_, status_, gear_;
	QImage head_;       // the picture on the slingshot, already cut to a circle
	QImage headScaled_; // head_ at the size it is drawn
	bool customHead_ = false;
	QImage shadow_;     // the soft shadow under a button
	double shadowScale_ = 0;
};
