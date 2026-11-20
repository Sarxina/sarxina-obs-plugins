// WheelOfYap's self-test: wheels of 1 to 120 topics, 300 spins that each stop on the slice they were planned for, a
// forced topic that is landed on, the two triggers' settings kept apart, and how long painting takes. With a folder,
// it writes pictures of a spin there; with WHEELOFYAP_SELFTEST_FRAMES=1 as well, every frame of one spin at 30 a
// second, at half size, in a folder "frames".
#include "self-test.hpp"

#include "chat-trigger.hpp"
#include "topics-dialog.hpp"
#include "wheel.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>

#include <obs.h>

#include <cmath>

using namespace sarxina;
using namespace sarxina::wheelofyap;

namespace {

QStringList defaultTopics()
{
	QStringList topics;
#ifdef WHEELOFYAP_TOPICS
	QFile file(QStringLiteral(WHEELOFYAP_TOPICS));
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream in(&file);
		in.setEncoding(QStringConverter::Utf8);
		while (!in.atEnd()) {
			const QString line = topicText(in.readLine());
			if (!line.isEmpty())
				topics.push_back(line);
		}
	}
#endif
	// Inside OBS the plugin's own topics.txt is not where the build left it, so the wheel gets made-up topics.
	if (topics.isEmpty())
		for (int i = 1; i <= 24; i++)
			topics.push_back(QStringLiteral("Topic %1").arg(i));
	return topics;
}

// Runs a spin to its stop at 60 frames a second; the slice under the pointer then.
int runSpin(Spinner &spinner, int &frames)
{
	frames = 0;
	while (frames < 60 * 30) {
		frames++;
		if (spinner.advance(1.0 / 60))
			break;
	}
	return spinner.sliceUnderPointer();
}

bool spins()
{
	bool ok = true;
	int landedRight = 0, total = 0;
	for (int count : {1, 2, 7, 30, 101, 120}) {
		QStringList labels;
		for (int i = 0; i < count; i++)
			labels.push_back(QStringLiteral("Topic %1").arg(i + 1));
		const auto face = paintFace(labels);
		Spinner spinner;
		spinner.show(face);
		for (uint32_t seed = 1; seed <= 50; seed++) {
			const Plan plan = planSpin(*face, QStringLiteral("viewer"), seed);
			spinner.start(face, plan);
			int frames = 0;
			const int landed = runSpin(spinner, frames);
			total++;
			if (landed == plan.target && !spinner.spinning() && spinner.landedOnce())
				landedRight++;
			else
				test::report(false, "%d topics, seed %u: planned slice %d, stopped on %d", count, seed,
					     plan.target, landed);
		}
	}
	ok = landedRight == total;
	test::report(ok, "%d of %d spins stopped on the slice they were planned for: %s", landedRight, total,
		     ok ? "OK" : "FAILED");
	return ok;
}

bool forced()
{
	const QStringList labels = defaultTopics();
	const QString topic = QStringLiteral("Why does Sarxina's cat hate him");
	const auto face = paintFace(labels, topic, 40);
	Spinner spinner;
	spinner.show(face);
	const Plan plan = planSpin(*face, QStringLiteral("Chatter"), 7);
	spinner.start(face, plan);
	int frames = 0;
	const int landed = runSpin(spinner, frames);
	const bool ok = plan.forced && landed == face->forced && face->labels[landed] == topic && plan.topic == topic &&
			spinner.ticks() > face->count();
	test::report(ok, "a forced topic on %d topics: stopped on slice %d (\"%s\") after %.1f s and %d slices past the "
			 "pointer: %s",
		     face->count(), landed, face->labels[landed].toUtf8().constData(), frames / 60.0, spinner.ticks(),
		     ok ? "OK" : "FAILED");
	return ok;
}

bool triggers()
{
	ChatTrigger spin("spin", TriggerSettings::Trigger::Reward), force("topic", TriggerSettings::Trigger::Reward);
	spin.key = "spin_";
	force.key = "force_";
	TriggerSettings a = spin.settings(), b = force.settings();
	a.rewardId = "spin-reward";
	a.cooldownSeconds = 12;
	b.rewardId = "force-reward";
	b.cooldownSeconds = 34;
	obs_data_t *settings = obs_data_create();
	triggerToSettings(settings, a, spin.key);
	triggerToSettings(settings, b, force.key);
	bool rewrite = false;
	spin.setSettings(spin.settings());
	const TriggerSettings backA = triggerFromSettings(settings, spin, rewrite);
	const TriggerSettings backB = triggerFromSettings(settings, force, rewrite);
	obs_data_release(settings);

	// A redeem of each reward goes to its own trigger only.
	spin.setSettings(backA);
	force.setSettings(backB);
	sarxina_chat_message message = {};
	message.user_name = "viewer";
	message.display_name = "Viewer";
	message.user_id = "1";
	message.text = "Cats";
	message.reward_id = "force-reward";
	Heard heard;
	const bool spun = spin.hear(message, 0, true, heard);
	const bool forcedHeard = force.hear(message, 0, true, heard);
	const bool ok = backA.rewardId == "spin-reward" && backA.cooldownSeconds == 12 &&
			backB.rewardId == "force-reward" && backB.cooldownSeconds == 34 && !spun && forcedHeard &&
			heard.text == QStringLiteral("Cats") && heard.who == QStringLiteral("Viewer");
	test::report(ok, "two triggers' settings kept apart, a forced topic's redeem heard by its trigger only: %s",
		     ok ? "OK" : "FAILED");
	return ok;
}

bool painting(const char *folder)
{
	const QStringList labels = defaultTopics();
	QElapsedTimer clock;
	clock.start();
	const auto face = paintFace(labels);
	const qint64 faceMs = clock.restart();
	const Plan plan = planSpin(*face, QStringLiteral("Sarxina"), 3);
	const qint64 planMs = clock.elapsed();
	test::report(true, "painting %d topics takes %lld ms, a spin's reveal %lld ms", face->count(), faceMs, planMs);
	if (!folder || !*folder)
		return !labels.isEmpty();

	QDir().mkpath(QString::fromUtf8(folder));
	const QImage hub = paintHub(), pointer = paintPointer();
	Spinner spinner;
	spinner.show(face);
	spinner.start(face, plan);
	const bool frames = qEnvironmentVariableIntValue("WHEELOFYAP_SELFTEST_FRAMES") != 0;
	const QString frameFolder = QString::fromUtf8(folder) + QStringLiteral("/frames");
	if (frames)
		QDir().mkpath(frameFolder);
	int written = 0;
	const double stills[] = {0.2, 1.5, 4.0, 7.0};
	size_t still = 0;
	double t = 0;
	int frame = 0;
	const double end = plan.seconds + 5;
	while (t < end) {
		spinner.advance(1.0 / 30);
		t += 1.0 / 30;
		if (frames)
			compose(spinner, hub, pointer, 0.5)
				.save(frameFolder + QStringLiteral("/%1.png").arg(frame, 4, 10, QLatin1Char('0')));
		frame++;
		if (still < std::size(stills) && t >= stills[still]) {
			compose(spinner, hub, pointer).save(QStringLiteral("%1/spin-%2s.png").arg(QString::fromUtf8(folder)).arg(stills[still]));
			written++;
			still++;
		}
	}
	compose(spinner, hub, pointer).save(QString::fromUtf8(folder) + QStringLiteral("/landed.png"));
	written++;

	const auto forcedFace = paintFace(labels, QStringLiteral("Why does Sarxina's cat hate him"), 12);
	Spinner forcedSpinner;
	forcedSpinner.start(forcedFace, planSpin(*forcedFace, QStringLiteral("xX_ChatGoblin_Xx"), 9));
	int count = 0;
	runSpin(forcedSpinner, count);
	for (int i = 0; i < 180; i++)
		forcedSpinner.advance(1.0 / 60);
	compose(forcedSpinner, hub, pointer).save(QString::fromUtf8(folder) + QStringLiteral("/forced.png"));
	written++;
	std::vector<Topic> rows;
	for (const QString &label : labels)
		rows.push_back({label, rows.size() % 9 != 4});
	TopicsDialog dialog(rows, nullptr, nullptr);
	dialog.grab().save(QString::fromUtf8(folder) + QStringLiteral("/topics-window.png"));
	written++;
	test::report(true, "%d pictures written to %s%s", written, folder, frames ? " (and every frame of the spin)" : "");
	return true;
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	const bool a = spins();
	const bool b = forced();
	const bool c = triggers();
	const bool d = painting(picturesFolder);
	const bool passed = a && b && c && d;
	test::report(passed, passed ? "passed" : "FAILED");
	return passed;
}
