// The part of JobApplication's self-test that needs OBS, the Sarxina OBS Plugin Manager and a running VTube Studio
// (JOBAPPLICATION_SELFTEST=1). It says whether the manager runs the plugin, its trigger and size, the model VTube Studio
// has loaded, the ArtMesh the paper would be pinned to, and where VTube Studio's Items folder is (for the Live2D item).
// It presses "Clear the application" in the settings the way the manager does, which takes off any lines chat wrote.
//
// With JOBAPPLICATION_SELFTEST_SHOW=1 it also brings the paper up the way the hotkey does, leaves it up for eight
// seconds, and takes it down again: the first time, VTube Studio asks the streamer once to allow custom images, and the
// check waits up to a minute for that.
#include "jobapplication.hpp"
#include "self-test.hpp"

#include <QDir>
#include <QJsonArray>
#include <QTimer>

#include <functional>
#include <memory>
#include <string>

using namespace sarxina;
using namespace sarxina::jobapplication;

namespace {

constexpr int kUpMs = 75000;   // the permission box, then the load
constexpr int kStayMs = 8000;  // up this long
constexpr int kDownMs = 10000;

// Waits for `done`, looking every 100 ms, then goes on; or says it waited too long.
void waitFor(QObject *context, std::function<bool()> done, int ms, std::string what, std::function<void(bool)> then)
{
	auto *timer = new QTimer(context);
	auto waited = std::make_shared<int>(0);
	timer->setInterval(100);
	QObject::connect(timer, &QTimer::timeout, context, [timer, waited, done, ms, what, then] {
		*waited += 100;
		if (done() || *waited >= ms) {
			timer->deleteLater();
			const bool ok = done();
			if (!ok)
				test::report(false, "%s: waited %d s", what.c_str(), ms / 1000);
			then(ok);
		}
	});
	timer->start();
}

// The button in the settings, pressed the way the manager presses it.
void pressClear(JobApplication &plugin)
{
	obs_properties_t *properties = plugin.properties();
	obs_property_t *clear = obs_properties_get(properties, "clear");
	const long long before = static_cast<long long>(plugin.entries());
	const bool redraw = clear && obs_property_button_clicked(clear, nullptr);
	const bool ok = clear && !redraw && plugin.entries() == 0;
	test::report(ok, "in OBS: pressed \"%s\" in the settings: %lld line%s before, %lld after: %s",
		     clear ? obs_property_description(clear) : "(no such button)", before, before == 1 ? "" : "s",
		     static_cast<long long>(plugin.entries()), ok ? "OK" : "FAILED");
	obs_properties_destroy(properties);
}

void showAndHide(JobApplication &plugin)
{
	PaperProp &prop = plugin.prop();
	plugin.toggle(QStringLiteral("the self-test"));
	waitFor(
		&plugin, [&prop] { return prop.shown() && !prop.busy(); }, kUpMs, "the paper coming up",
		[&plugin, &prop](bool up) {
			if (!up) {
				test::report(false, "in OBS: FAILED (the log above says why)");
				return;
			}
			test::report(!prop.pinnedTo().isEmpty(), "in OBS: the paper is up as item %s, pinned to %s: %s",
				     prop.instance().toUtf8().constData(),
				     prop.pinnedTo().isEmpty() ? "nothing" : prop.pinnedTo().toUtf8().constData(),
				     prop.pinnedTo().isEmpty() ? "NOT PINNED" : "OK");
			test::report(prop.bends(), "in OBS: %s%s%s: %s",
				     prop.bends() ? "it is the Live2D item " : "it is the flat picture, not the Live2D item",
				     prop.bends() ? prop.itemName().toUtf8().constData() : "",
				     prop.bends() ? (" in " + QDir::toNativeSeparators(prop.itemsFolder())).toUtf8().constData() : "",
				     prop.bends() ? "OK" : "DOES NOT BEND (the log above says why)");
			QTimer::singleShot(kStayMs, &plugin, [&plugin, &prop] {
				plugin.toggle(QStringLiteral("the self-test"));
				waitFor(
					&plugin, [&prop] { return !prop.shown() && !prop.busy(); }, kDownMs,
					"the paper going down", [](bool down) {
						test::report(down, "in OBS: the paper went down again: %s", down ? "OK" : "FAILED");
						test::report(down, "in OBS: %s", down ? "passed" : "FAILED");
					});
			});
		});
}

} // namespace

void checkInObs(JobApplication &plugin)
{
	const TriggerSettings &trigger = plugin.settings();
	test::report(true, "in OBS: %s; %s; chat: %s, cooldown %d s; size %d %%",
		     plugin.managed() ? "run by the Sarxina OBS Plugin Manager" : "the manager is NOT installed",
		     plugin.enabled() ? "on" : "off",
		     trigger.trigger == TriggerSettings::Trigger::Command ? ("!" + trigger.command).c_str()
									    : "a channel point reward",
		     trigger.cooldownSeconds, plugin.sizePercent());
	if (!plugin.managed())
		return;
	pressClear(plugin);

	const bool show = qEnvironmentVariableIntValue("JOBAPPLICATION_SELFTEST_SHOW") != 0;
	JobApplication *at = &plugin;
	sendToVts(QStringLiteral("CurrentModelRequest"), {}, [at, show](bool ok, const QJsonObject &model) {
		if (!ok || !model.value(QStringLiteral("modelLoaded")).toBool()) {
			test::report(false, "in OBS: VTube Studio: %s",
				     ok ? "no model loaded"
					: model.value(QStringLiteral("message")).toString().toUtf8().constData());
			return;
		}
		const QString items = findItemsFolder(QStringLiteral("Items"));
		test::report(!items.isEmpty(), "in OBS: VTube Studio's Items folder: %s",
			     items.isEmpty() ? "NOT FOUND (the paper will come up as a flat picture)"
					     : QDir::toNativeSeparators(items).toUtf8().constData());
		const QJsonObject position = model.value(QStringLiteral("modelPosition")).toObject();
		test::report(true, "in OBS: VTube Studio has %s loaded, at %.2f, %.2f, size %.1f, turned %.1f degrees",
			     model.value(QStringLiteral("modelName")).toString().toUtf8().constData(),
			     position.value(QStringLiteral("positionX")).toDouble(),
			     position.value(QStringLiteral("positionY")).toDouble(),
			     position.value(QStringLiteral("size")).toDouble(),
			     position.value(QStringLiteral("rotation")).toDouble());
		sendToVts(QStringLiteral("ArtMeshListRequest"), {}, [at, show](bool listed, const QJsonObject &meshes) {
			QStringList names;
			for (const QJsonValue &name : meshes.value(QStringLiteral("artMeshNames")).toArray())
				names << name.toString();
			const QString pin = pinTarget(names);
			test::report(listed && !pin.isEmpty(), "in OBS: %lld ArtMeshes; the paper pins to %s",
				     static_cast<long long>(names.size()),
				     pin.isEmpty() ? "none of them" : pin.toUtf8().constData());
			if (show)
				showAndHide(*at);
		});
	});
}
