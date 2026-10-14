#include "self-test.hpp"

#include "controls-dock.hpp"

#include <obs-module.h>

#include <QDir>

#include <cstdarg>

namespace sarxina::test {

void report(bool ok, const char *format, ...)
{
	const QByteArray line = QByteArray("[") + obs_module_name() + "] self-test: " + format;
	va_list args;
	va_start(args, format);
	blogva(ok ? LOG_INFO : LOG_WARNING, line.constData(), args);
	va_end(args);
}

std::string names(const std::vector<Control> &controls)
{
	std::string list;
	for (Control control : controls) {
		if (!list.empty())
			list += ", ";
		list += obs_module_text(info(control).label);
	}
	return list.empty() ? "nothing" : list;
}

Pictures::Pictures(const char *folder) : folder_(QString::fromUtf8(folder ? folder : ""))
{
	if (wanted() && !QDir().mkpath(folder_)) {
		report(false, "cannot create %s", folder_.toUtf8().constData());
		folder_.clear();
	}
}

Pictures::~Pictures()
{
	if (wanted())
		report(true, "wrote %d pictures of the dock to %s", written_, folder_.toUtf8().constData());
}

void Pictures::save(ControlsDock &dock, const char *title)
{
	if (!wanted())
		return;
	for (int ratio : {1, 2}) {
		const QString file = QDir(folder_).filePath(QStringLiteral("%1@%2x.png").arg(QLatin1String(title)).arg(ratio));
		written_ += dock.snapshot(ratio).save(file, "PNG");
	}
}

void settle(ControlsDock &dock, int width, int height)
{
	dock.setPanelSize(width, height);
	dock.advance(0.5);
}

} // namespace sarxina::test
