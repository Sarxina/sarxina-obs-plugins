// PlinkoToggle's self-test (see shared/src/self-test.hpp). It needs no OBS, no chat and no VTube Studio:
//   1. Boards with 1 to 40 toggles. Each grows with its toggles: from five toggles on, every one more makes it a slot
//      wider, and the picture with it; the bottom row of pegs reaches into both end slots. 500 balls on each, every
//      one has to come down in one slot within 15 seconds. A ball falls the same way with other balls falling beside
//      it, so a seed always brings it to the same slot.
//   2. Chat: the command and the redeem drop balls when they should, the cooldown, redeems waiting their turn, the
//      rewards offered in the settings, and how a chatter's name is shown.
//   3. VTube Studio's answers read into toggles.
//   4. How long a frame takes.
// Given a folder, it also paints boards into PNG files there. Inside OBS, in-obs-check.cpp goes on from here.
#include "self-test.hpp"

#include "chat-trigger.hpp"
#include "plain-text.hpp"
#include "toggle-board.hpp"
#include "vts-toggles.hpp"

#include <QDir>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace sarxina;
using namespace sarxina::plinkotoggle;

namespace {

constexpr int kDrops = 500;
constexpr double kDownSeconds = 15; // every ball has to be down in a slot by then

// Counts checks and keeps the first few that failed, for one line of results.
struct Tally {
	int passed = 0, total = 0;
	std::string failed;

	void expect(bool ok, const std::string &what)
	{
		total++;
		passed += ok;
		if (!ok && failed.size() < 300)
			failed += (failed.empty() ? "; failed: " : ", ") + what;
	}
	bool report(const char *group, const char *what)
	{
		const bool ok = passed == total;
		test::report(ok, "%s: %d of %d %s%s: %s", group, passed, total, what, failed.c_str(), ok ? "OK" : "FAILED");
		return ok;
	}
};

const char *const kNames[] = {"Glasses",   "Heart Eyes", "Blush",  "Tears",     "Angry Mark", "Sweat Drop", "Hat",
			      "Cat Ears",  "Wave",       "Dizzy",  "Sparkles",  "Pout",       "Wink",       "Shock",
			      "Sleepy",    "Thumbs Up",  "Mic",    "Headphones"};

Content toggles(int count, bool longNames = false)
{
	Content content;
	content.label = QStringLiteral("!plinko");
	for (int i = 0; i < count; i++) {
		Toggle toggle;
		toggle.id = "hotkey-" + std::to_string(i);
		toggle.type = "ToggleExpression";
		QString name = QString::fromLatin1(kNames[i % std::size(kNames)]);
		if (i >= static_cast<int>(std::size(kNames)))
			name += QStringLiteral(" %1").arg(i / std::size(kNames) + 1);
		if (longNames && i % 3 == 0)
			name += QStringLiteral(" (the big one)");
		toggle.name = name;
		content.toggles.push_back(toggle);
	}
	return content;
}

// Drops one ball with this seed and plays on until it is down. The slot, or -1 if it was not down in time.
int dropOne(ToggleBoard &board, uint32_t seed, double &seconds)
{
	board.drop(QString(), seed);
	std::vector<Landing> landed;
	int steps = 0;
	for (; steps < kDownSeconds * 120 && landed.empty(); steps++)
		board.advance(kStep, landed);
	seconds = steps / 120.0;
	return landed.size() == 1 ? landed[0].slot : -1;
}

// ---- 1. the boards --------------------------------------------------------------------------------------------------

bool boards()
{
	bool all = true;
	double lastWidth = 0;
	int lastRows = 0;
	for (const int count : {1, 2, 3, 6, 12, 24, 40}) {
		ToggleBoard board;
		board.setContent(toggles(count, count == 6));
		const Plinko &plinko = board.plinko();
		const int rows = plinko.shape().rows;
		const bool grows = board.width() == std::max(count, 5) * 56.0 && board.width() >= lastWidth &&
				   rows >= lastRows && board.pictureSize() == QSize(static_cast<int>(board.width() * 2),
										      static_cast<int>(std::ceil(board.height() * 2)));
		const bool reaches = plinko.peg(rows - 1, 0).x < plinko.slotWidth() &&
				     plinko.peg(rows - 1, rows - 1).x > board.width() - plinko.slotWidth();
		lastWidth = board.width();
		lastRows = rows;
		std::vector<int> landedIn;
		std::set<int> reached;
		double slowest = 0;
		int down = 0;
		for (int i = 0; i < kDrops; i++) {
			double seconds = 0;
			const int slot = dropOne(board, static_cast<uint32_t>(i + 1), seconds);
			landedIn.push_back(slot);
			if (slot >= 0) {
				down++;
				reached.insert(slot);
				slowest = std::max(slowest, seconds);
			}
			// Let it rest and fade, as it would between drops.
			std::vector<Landing> later;
			board.advance(2.0, later);
		}

		// The first 50 again, each one dropped while another ball is still falling beside it.
		ToggleBoard busy;
		busy.setContent(toggles(count, count == 6));
		int same = 0;
		for (int i = 0; i < 50; i++) {
			busy.drop(QString(), 999999u - static_cast<uint32_t>(i));
			std::vector<Landing> early;
			for (int s = 0; s < 30; s++)
				busy.advance(kStep, early);
			busy.drop(QStringLiteral("x"), static_cast<uint32_t>(i + 1));
			std::vector<Landing> landed;
			for (int s = 0; s < kDownSeconds * 120 && landed.size() < 2; s++)
				busy.advance(kStep, landed);
			const auto mine = std::find_if(landed.begin(), landed.end(), [](const Landing &l) { return l.who == "x"; });
			same += mine != landed.end() && mine->slot == landedIn[static_cast<size_t>(i)];
			busy.advance(2.0, landed);
		}

		const bool ok = grows && reaches && down == kDrops && same == 50;
		all = all && ok;
		test::report(ok,
			     "%d toggle(s): a board of %.0f x %.0f units (%d x %d pixels), %d rows of pegs%s, names %s; %d balls "
			     "-> %d came down in one slot each (the slowest after %.1f seconds), in %zu of %d slots; the first "
			     "50 again beside other balls -> %d in the same slot: %s",
			     count, board.width(), board.height(), board.pictureSize().width(), board.pictureSize().height(), rows,
			     reaches ? " reaching into both end slots" : " NOT reaching the end slots",
			     board.verticalLabels() ? "upwards" : "across", kDrops, down, slowest, reached.size(), count, same,
			     ok ? "OK" : "FAILED");
	}

	// No toggles: no balls.
	ToggleBoard empty;
	Content none;
	none.label = QStringLiteral("!plinko");
	empty.setContent(none);
	empty.drop(QStringLiteral("someone"), 1);
	const bool takesNone = !empty.canDrop() && empty.falling() == 0 && empty.sprites().empty();
	test::report(takesNone, "a board without toggles takes no ball: %s", takesNone ? "OK" : "FAILED");
	return all && takesNone;
}

// ---- 2. chat --------------------------------------------------------------------------------------------------------

struct Message {
	std::string text, reward, display = "Viewer", user = "viewer";
	sarxina_chat_message view() const
	{
		sarxina_chat_message m{};
		m.platform = "twitch";
		m.id = "id";
		m.user_id = "1";
		m.user_name = user.c_str();
		m.display_name = display.c_str();
		m.text = text.c_str();
		m.reward_id = reward.c_str();
		return m;
	}
};

bool chat()
{
	Tally tally;
	QString who;
	const auto hear = [&who](ChatTrigger &drops, const Message &message, double ms, bool ready = true) {
		const sarxina_chat_message m = message.view();
		return drops.hear(m, ms, ready, who);
	};

	// The command
	ChatTrigger drops{"plinko"};
	tally.expect(drops.settings().command == "plinko" && drops.settings().cooldownSeconds == 10, "!plinko and 10 s");
	tally.expect(!hear(drops, {"!plinko"}, 0, false), "not while no board can take a ball");
	tally.expect(hear(drops, {"!plinko"}, 10) && who == "Viewer", "!plinko");
	tally.expect(!hear(drops, {"!plinko"}, 5000), "again 5 s later, cooling down");
	tally.expect(hear(drops, {"!PLINKO go go"}, 10010), "in capitals with words after it, once cooled down");
	for (const char *other : {"!plinkos", "plinko", " !plinko", "!plink", "! plinko", "!plinko\t"})
		tally.expect(!hear(drops, {other}, 1e6), std::string("not the command: \"") + other + "\"");
	TriggerSettings settings;
	settings.command = "drop_it";
	settings.cooldownSeconds = 1;
	drops.setSettings(settings);
	tally.expect(!hear(drops, {"!plinko"}, 2e6) && hear(drops, {"!drop_it"}, 2e6), "another command");
	tally.expect(!hear(drops, {"!drop_it", "a-reward"}, 2e6 + 500) && drops.waiting() == 0,
		     "a redeem in command mode is chat like any other");

	// The redeem
	settings.trigger = TriggerSettings::Trigger::Reward;
	settings.rewardId = "the-reward";
	settings.cooldownSeconds = 10;
	ChatTrigger redeems{"plinko"};
	redeems.setSettings(settings);
	tally.expect(!hear(redeems, {"!plinko"}, 0) && !hear(redeems, {"hi", "other-reward"}, 0),
		     "the command and other rewards drop nothing");
	tally.expect(hear(redeems, {"go", "the-reward", "First"}, 0) && who == "First", "the reward drops a ball");
	tally.expect(!hear(redeems, {"me too", "the-reward", "Second"}, 1000) && redeems.waiting() == 1,
		     "the next one, cooling down, waits");
	tally.expect(!redeems.next(9000, true, who) && redeems.waiting() == 1, "still waiting 9 s on");
	tally.expect(!redeems.next(10500, false, who), "and while no board can take a ball");
	tally.expect(redeems.next(10500, true, who) && who == "Second" && redeems.waiting() == 0,
		     "dropping once cooled down");
	for (int i = 0; i < 60; i++)
		hear(redeems, {"spam", "the-reward"}, 11000);
	tally.expect(redeems.waiting() == kMostWaiting, "at most 50 waiting");
	settings.trigger = TriggerSettings::Trigger::Command;
	redeems.setSettings(settings);
	tally.expect(redeems.waiting() == 0, "none waiting once the trigger changes");

	// The rewards offered in the settings
	ChatTrigger seen{"plinko"};
	const Message first{"drop it please", "reward-a", "Sarxina"};
	seen.hear(first.view(), 0, true, who);
	tally.expect(seen.rewardsChanged() && seen.rewards().size() == 1 && seen.rewards()[0].id == "reward-a" &&
			     seen.rewards()[0].label == "Sarxina: drop it please",
		     "a reward redeemed in chat, offered by who redeemed it and what they wrote");
	seen.hear(Message{"hello", ""}.view(), 1, true, who);
	tally.expect(!seen.rewardsChanged(), "chat without a reward offers nothing new");
	seen.hear(Message{"b", "reward-b"}.view(), 2, true, who);
	seen.hear(Message{"again", "reward-a", "Other"}.view(), 3, true, who);
	tally.expect(!seen.rewardsChanged() && seen.rewards().size() == 2 && seen.rewards()[0].id == "reward-a" &&
			     seen.rewards()[0].label == "Other: again",
		     "the latest first, each once");

	// Names
	const auto shown = [](const std::string &display, const std::string &user) {
		Message message{"!plinko"};
		message.display = display;
		message.user = user;
		return chatterName(message.view());
	};
	tally.expect(shown("Sarxina", "sarxina") == "Sarxina", "a display name");
	tally.expect(shown("", "sarxina") == "sarxina", "the account name when there is no display name");
	tally.expect(shown("\xE2\x80\xAE" "anixraS", "x") == "anixraS", "a name that turns the text around, not turned");
	tally.expect(shown("a\xE2\x80\x8B" "b  c", "x") == "ab c", "invisible characters and spaces taken out");
	tally.expect(shown(std::string(40, 'w'), "x") == QString(24, QLatin1Char('w')) + QChar(0x2026),
		     "a long name cut to 25 characters");
	tally.expect(shown("<b>bold</b>", "x") == "<b>bold</b>", "markup shown as the text it is");

	// Commands as typed in the settings
	tally.expect(commandName("!Plinko") == "plinko" && commandName("plinko") == "plinko" &&
			     commandName(" !!drop_it ") == "drop_it",
		     "commands typed with or without the !");
	for (const char *bad : {"", "!", "two words", "drop-it", "abcdefghijklmnopqrstuvwxyz"})
		tally.expect(commandName(bad).empty(), std::string("not a command: \"") + bad + "\"");
	return tally.report("chat", "checks as expected");
}

// ---- 3. VTube Studio's answers --------------------------------------------------------------------------------------

bool answers()
{
	Tally tally;
	const char *json = R"({"modelLoaded":true,"modelName":"Sarxina","modelID":"m1","availableHotkeys":[
		{"name":"Glasses","type":"ToggleExpression","file":"Glasses.exp3.json","hotkeyID":"h1"},
		{"name":"","type":"ToggleExpression","file":"expressions/Heart Eyes.exp3.json","hotkeyID":"h2"},
		{"name":"Nothing","type":"Unset","file":"","hotkeyID":"h3"},
		{"name":"","type":"TriggerAnimation","file":"","hotkeyID":"h4"},
		{"name":"No id","type":"ToggleExpression","file":"","hotkeyID":""}]})";
	const Model model = readHotkeys(true, json);
	tally.expect(model.state == Model::State::Loaded && model.name == "Sarxina" && model.id == "m1", "a loaded model");
	tally.expect(model.toggles.size() == 3, "its hotkeys, without the one set to do nothing and the one without an id");
	tally.expect(model.toggles.size() == 3 && model.toggles[0].name == "Glasses" && model.toggles[0].id == "h1" &&
			     model.toggles[1].name == "Heart Eyes" && model.toggles[2].name == "TriggerAnimation",
		     "named by their name, else their file, else what they do");
	tally.expect(readHotkeys(true, R"({"modelLoaded":false})").state == Model::State::NoModel, "no model loaded");
	tally.expect(readHotkeys(false, R"({"message":"not connected to VTube Studio"})").state ==
			     Model::State::NotConnected,
		     "not connected");
	tally.expect(readHotkeys(true, "not json").state == Model::State::NoModel, "an answer that is not JSON");
	return tally.report("VTube Studio", "answers read as expected");
}

// ---- 4. painting ----------------------------------------------------------------------------------------------------

// What a source does each frame while a ball falls: move it on and list what to draw. The still picture is painted
// only when the toggles change, and how long that takes is measured once, on the biggest board here.
void speed()
{
	ToggleBoard board;
	board.setContent(toggles(40));
	QElapsedTimer still;
	still.start();
	const QSize size = board.still().size();
	const double stillMs = still.nsecsElapsed() / 1e6;

	board.drop(QStringLiteral("Viewer"), 7);
	QElapsedTimer timer;
	timer.start();
	std::vector<Landing> landed;
	size_t drawn = 0;
	for (int i = 0; i < 600; i++) {
		board.advance(1.0 / 60, landed);
		drawn += board.sprites().size();
	}
	test::report(true, "40 toggles: the still %d x %d picture painted in %.1f ms; a frame with a ball falling: %.3f ms "
			   "to move it on and list the %.1f small pictures to draw over it",
		     size.width(), size.height(), stillMs, timer.nsecsElapsed() / 1e6 / 600, drawn / 600.0);
}

// ---- pictures -------------------------------------------------------------------------------------------------------

void pictures(const char *folder)
{
	if (!folder || !*folder || !QDir().mkpath(QString::fromUtf8(folder)))
		return;
	const QDir dir(QString::fromUtf8(folder));
	int written = 0;
	QImage picture;
	const auto save = [&](ToggleBoard &board, const char *title) {
		board.paint(picture);
		written += picture.save(dir.filePath(QString::fromLatin1(title) + QStringLiteral(".png")), "PNG");
	};
	std::vector<Landing> landed;

	ToggleBoard board;
	board.setContent(toggles(8));
	save(board, "01-ready");
	board.drop(QStringLiteral("CoolViewer_99"), 3);
	board.advance(1.4, landed);
	save(board, "02-falling");
	while (landed.empty())
		board.advance(kStep, landed);
	board.advance(0.05, landed);
	save(board, "03-landed");

	ToggleBoard few;
	few.setContent(toggles(3));
	save(few, "04-three-toggles");

	ToggleBoard many;
	many.setContent(toggles(30, true));
	for (uint32_t seed = 1; seed <= 4; seed++) {
		many.drop(QStringLiteral("Viewer %1").arg(seed), seed);
		many.advance(0.45, landed);
	}
	save(many, "05-thirty-toggles");

	ToggleBoard off;
	off.setContent(Content{});
	save(off, "06-no-toggles");

	ToggleBoard redeem;
	Content reward = toggles(5);
	reward.label = QStringLiteral("Drop a Sarxina");
	redeem.setContent(reward);
	save(redeem, "08-reward");

	ToggleBoard names;
	Content content = toggles(6, true);
	names.setContent(content);
	names.drop(plainText(QChar(0x202E) + QStringLiteral("eman_gnol_yrev_a_si_siht_dna_more"), 25), 5);
	names.advance(0.3, landed);
	save(names, "07-long-names");

	test::report(true, "wrote %d pictures to %s", written, folder);
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	bool passed = boards();
	passed &= chat();
	passed &= answers();
	speed();
	pictures(picturesFolder);
	test::report(passed, "%s", passed ? "passed" : "FAILED");
	return passed;
}
