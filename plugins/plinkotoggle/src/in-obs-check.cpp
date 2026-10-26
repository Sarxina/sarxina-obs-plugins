// The part of PlinkoToggle's self-test that needs OBS, the Sarxina OBS Plugin Manager and a running VTube Studio
// (PLINKOTOGGLE_SELFTEST=1). It says whether the manager runs the plugin, how many boards there are and whether one is
// on screen, and the toggles read from the model VTube Studio has loaded.
//
// With PLINKOTOGGLE_SELFTEST_TOGGLE=<the name of an expression toggle> it also plays chat at the boards, the same
// way the manager hands chat over, and every ball is seeded to come down on that toggle:
//   1. "!<command>" drops a ball. Its landing switches the toggle in VTube Studio (read back from VTube Studio), and
//      the check switches it back.
//   2. The same command again at once is ignored: the board is cooling down.
//   3. A redeem of another reward drops nothing; one of the reward picked drops a ball, switched and switched back.
//   4. Two redeems at once: one drops now and one waits for the cooldown. Both switch the toggle, so it ends as it
//      was, which is checked.
// The settings are only tried for the check, never saved, and put back at the end.
#include "board-source.hpp"
#include "plinkotoggle.hpp"
#include "self-test.hpp"

#include <obs.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace sarxina;
using namespace sarxina::plinkotoggle;

namespace {

constexpr const char *kTestReward = "plinkotoggle-selftest-reward";
constexpr int kTestCooldown = 3; // seconds
constexpr double kLandingSeconds = 25;

// One step after another, each waiting for what it needs. A step that waits too long ends the check.
class Check : public QObject {
public:
	explicit Check(PlinkoToggle &plugin) : plugin_(plugin), original_(plugin.settings()) {}

	using Step = std::function<void()>;

	void waitFor(std::function<bool()> done, int ms, std::string what, Step then)
	{
		auto *timer = new QTimer(this);
		auto started = std::make_shared<qint64>(0);
		timer->setInterval(50);
		connect(timer, &QTimer::timeout, this, [this, timer, started, done, ms, what, then] {
			*started += 50;
			if (done()) {
				timer->deleteLater();
				then();
			} else if (*started >= ms) {
				timer->deleteLater();
				fail(what + ": waited " + std::to_string(ms / 1000) + " s");
			}
		});
		timer->start();
	}
	void after(int ms, Step then) { QTimer::singleShot(ms, this, then); }

	void expect(bool ok, const std::string &what)
	{
		test::report(ok, "%s: %s", what.c_str(), ok ? "OK" : "FAILED");
		passed_ += ok;
		total_++;
	}
	void fail(const std::string &what)
	{
		expect(false, what);
		finish();
	}
	void finish()
	{
		plugin_.trySettings(original_);
		plugin_.fired = nullptr;
		plugin_.seeds.clear();
		test::report(passed_ == total_, "in OBS: %d of %d as expected: %s", passed_, total_,
			     passed_ == total_ ? "passed" : "FAILED");
		deleteLater();
	}

	// A request to VTube Studio, sent through the manager.
	void vts(const char *type, const QJsonObject &data, std::function<void(bool, const QJsonObject &)> done)
	{
		auto *answer = new std::function<void(bool, const QJsonObject &)>(std::move(done));
		const QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact);
		const sarxina_vts_request request{type, json.constData(),
						  [](void *param, bool ok, const char *reply) {
							  std::unique_ptr<std::function<void(bool, const QJsonObject &)>>
								  call(static_cast<std::function<void(bool, const QJsonObject &)> *>(
									  param));
							  (*call)(ok, QJsonDocument::fromJson(reply).object());
						  },
						  answer};
		if (!sarxina_obs_plugin_manager_vts_request(&request)) {
			delete answer;
			fail("a request to VTube Studio");
		}
	}
	// Whether the expression is on, read from VTube Studio.
	void expressionOn(std::function<void(bool on)> then)
	{
		vts("ExpressionStateRequest",
		    {{"details", false}, {"expressionFile", QString::fromStdString(target_.file)}},
		    [this, then](bool ok, const QJsonObject &data) {
			    for (const QJsonValue &e : data.value("expressions").toArray())
				    if (ok && e.toObject().value("file").toString().toStdString() == target_.file) {
					    then(e.toObject().value("active").toBool());
					    return;
				    }
			    fail("reading " + target_.name.toStdString() + " from VTube Studio");
		    });
	}
	void trigger(Step then)
	{
		vts("HotkeyTriggerRequest", {{"hotkeyID", QString::fromStdString(target_.id)}},
		    [this, then](bool ok, const QJsonObject &) {
			    if (!ok)
				    fail("switching " + target_.name.toStdString() + " back");
			    else
				    then();
		    });
	}

	// What the plugin hears from chat, the way the manager hands it over.
	void say(const char *text, const char *reward = "")
	{
		sarxina_chat_message message{};
		message.platform = "twitch";
		message.id = "plinkotoggle-selftest";
		message.user_id = "0";
		message.user_name = "plinkotoggle_test";
		message.display_name = "PlinkoToggle test";
		message.role = SARXINA_ROLE_EVERYONE;
		message.text = text;
		message.time_ms = 0;
		message.reward_id = reward;
		plugin_.chat(message);
	}

	// Waits for `count` landings since the count was last set to 0, and checks that VTube Studio has the expression
	// `on` after them.
	void land(int count, bool on, const std::string &what, Step then)
	{
		waitFor([this, count] { return landed_ >= count; }, static_cast<int>(kLandingSeconds * 1000) * count,
			what + ": the ball coming down on " + target_.name.toStdString(), [this, on, what, then] {
				after(700, [this, on, what, then] {
					expressionOn([this, on, what, then](bool now) {
						expect(now == on, what + ": " + target_.name.toStdString() + " " +
									  (on ? "on" : "off") + " in VTube Studio");
						then();
					});
				});
			});
	}
	// Switches the toggle back and checks it.
	void putBack(const std::string &what, Step then)
	{
		trigger([this, what, then] {
			after(700, [this, what, then] {
				expressionOn([this, what, then](bool now) {
					expect(now == startedOn_, what + ": switched back");
					then();
				});
			});
		});
	}

	void run(const QString &name);

private:
	PlinkoToggle &plugin_;
	TriggerSettings original_;
	Toggle target_;
	uint32_t seed_ = 0;
	bool startedOn_ = false;
	int landed_ = 0;
	int passed_ = 0, total_ = 0;
};

// A seed that brings a ball down in `slot` on a board showing `content`.
uint32_t seedFor(const Content &content, int slot)
{
	ToggleBoard board;
	board.setContent(content);
	for (uint32_t seed = 1; seed < 20000; seed++) {
		board.drop(QString(), seed);
		std::vector<Landing> landed;
		for (int i = 0; i < 120 * 30 && landed.empty(); i++)
			board.advance(kStep, landed);
		if (!landed.empty() && landed[0].slot == slot)
			return seed;
	}
	return 0;
}

void Check::run(const QString &name)
{
	const Content shown = shownOnBoards();
	int slot = -1;
	for (size_t i = 0; i < shown.toggles.size(); i++)
		if (shown.toggles[i].name == name)
			slot = static_cast<int>(i);
	if (slot < 0 || shown.toggles[static_cast<size_t>(slot)].type != "ToggleExpression") {
		fail("PLINKOTOGGLE_SELFTEST_TOGGLE is not the name of an expression toggle on the board");
		return;
	}
	target_ = shown.toggles[static_cast<size_t>(slot)];
	seed_ = seedFor(shown, slot);
	if (!seed_) {
		fail("finding a seed for " + target_.name.toStdString());
		return;
	}
	test::report(true, "%s is slot %d of %zu; a ball with seed %u comes down in it", name.toUtf8().constData(),
		     slot + 1, shown.toggles.size(), seed_);
	plugin_.fired = [this](const Landing &landing, bool ok) {
		expect(ok && landing.toggle.id == target_.id, "a landing switched " + landing.toggle.name.toStdString() +
								 " for " + landing.who.toStdString());
		landed_++;
	};

	TriggerSettings command = original_;
	command.trigger = TriggerSettings::Trigger::Command;
	command.cooldownSeconds = kTestCooldown;
	plugin_.trySettings(command);
	const std::string typed = "!" + command.command;

	expressionOn([this, typed](bool on) {
		startedOn_ = on;
		test::report(true, "%s is %s in VTube Studio to begin with", target_.name.toUtf8().constData(),
			     on ? "on" : "off");

		// 1 and 2
		plugin_.seeds = {seed_};
		landed_ = 0;
		const int before = plugin_.dropped();
		say(typed.c_str());
		expect(plugin_.dropped() == before + 1, typed + " in chat dropping a ball");
		say(typed.c_str());
		expect(plugin_.dropped() == before + 1, "the same at once, ignored while cooling down");
		land(1, !startedOn_, "the command", [this] {
			putBack("the command", [this] {
				// 3
				TriggerSettings reward = original_;
				reward.trigger = TriggerSettings::Trigger::Reward;
				reward.rewardId = kTestReward;
				reward.cooldownSeconds = kTestCooldown;
				plugin_.trySettings(reward);
				const int before = plugin_.dropped();
				say("hello", "another-reward");
				expect(plugin_.dropped() == before, "a redeem of another reward, ignored");
				plugin_.seeds = {seed_};
				landed_ = 0;
				say("drop it", kTestReward);
				expect(plugin_.dropped() == before + 1, "a redeem of the reward dropping a ball");
				land(1, !startedOn_, "the redeem", [this] {
					putBack("the redeem", [this] {
						// 4
						after(kTestCooldown * 1000, [this] {
							const int before = plugin_.dropped();
							plugin_.seeds = {seed_, seed_};
							landed_ = 0;
							say("one", kTestReward);
							say("two", kTestReward);
							expect(plugin_.dropped() == before + 1 && plugin_.waiting() == 1,
							       "two redeems at once: one ball now, one waiting");
							waitFor([this, before] { return plugin_.dropped() == before + 2; },
								(kTestCooldown + 2) * 1000, "the waiting redeem dropping",
								[this] {
									expect(true, "the waiting redeem dropping after the cooldown");
									land(2, startedOn_, "two redeems", [this] { finish(); });
								});
						});
					});
				});
			});
		});
	});
}

} // namespace

// The settings as the manager draws them behind the gear: the command field for a command, the reward fields for a
// reward.
void settingsShown(PlinkoToggle &plugin)
{
	obs_properties_t *properties = plugin.properties();
	OBSDataAutoRelease settings = obs_data_create();
	plugin.getSettings(settings);
	const std::string command = obs_data_get_string(settings, "command");
	const long long cooldown = obs_data_get_int(settings, "cooldown");
	const auto shown = [properties](const char *name) {
		obs_property_t *property = obs_properties_get(properties, name);
		return property && obs_property_visible(property);
	};
	obs_data_set_string(settings, "trigger", "command");
	obs_properties_apply_settings(properties, settings);
	const bool forCommand = shown("trigger") && shown("command") && !shown("reward") && !shown("reward_help") &&
				!shown("reward_name") && shown("cooldown");
	obs_data_set_string(settings, "trigger", "reward");
	obs_properties_apply_settings(properties, settings);
	const bool forReward = !shown("command") && shown("reward") && shown("reward_help") && shown("reward_name") &&
			       shown("cooldown");
	obs_properties_destroy(properties);
	test::report(forCommand && forReward,
		     "settings behind the gear: the command (now %s) for a command, the reward, how to pick it and its "
		     "name for a reward, the cooldown (now %lld s) for both: %s",
		     command.c_str(), cooldown, forCommand && forReward ? "OK" : "FAILED");
}

void checkInObs(PlinkoToggle &plugin)
{
	test::report(plugin.managed(), "run by the Sarxina OBS Plugin Manager: %s, added: %s", plugin.managed() ? "yes" : "no",
		     plugin.enabled() ? "yes" : "no");
	test::report(true, "%d board(s), %s on screen", boardCount(), boardOnScreen() ? "one" : "none");
	settingsShown(plugin);

	auto *check = new Check(plugin);
	check->waitFor([&plugin] { return plugin.model().state == Model::State::Loaded; }, 30000,
		       "VTube Studio's model", [check, &plugin] {
			       const Model &model = plugin.model();
			       std::string names;
			       for (const Toggle &toggle : model.toggles)
				       names += (names.empty() ? "" : ", ") + toggle.name.toStdString() + " (" + toggle.type + ")";
			       test::report(!model.toggles.empty(), "VTube Studio: %s, %zu toggles: %s",
					    model.name.toUtf8().constData(), model.toggles.size(), names.c_str());
			       const QString toggle = QString::fromUtf8(qgetenv("PLINKOTOGGLE_SELFTEST_TOGGLE"));
			       if (toggle.isEmpty()) {
				       check->finish();
				       return;
			       }
			       check->waitFor([&plugin] { return plugin.ready(); }, 10000, "a board on screen with the toggles",
					      [check, toggle] { check->run(toggle); });
		       });
}
