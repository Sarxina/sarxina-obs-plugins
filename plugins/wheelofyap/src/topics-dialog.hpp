// The window with the wheel's topics, one row each: tick a row on or off, double-click to change it, drag or Up and
// Down to move it, Add and Remove. Save hands the list back; the wheel shows it at once.
#pragma once

#include "wheel.hpp"

#include <QDialog>

#include <functional>
#include <vector>

class QLabel;
class QListWidget;

namespace sarxina::wheelofyap {

class TopicsDialog : public QDialog {
public:
	TopicsDialog(const std::vector<Topic> &topics, std::function<void(std::vector<Topic>)> saved, QWidget *parent);

private:
	void add();
	void remove();
	void move(int by);
	void count();
	std::vector<Topic> topics() const;

	QListWidget *list_ = nullptr;
	QLabel *count_ = nullptr;
	std::function<void(std::vector<Topic>)> saved_;
};

} // namespace sarxina::wheelofyap
