// JobApplication's self-test (see shared/src/self-test.hpp). It needs no OBS, no chat and no VTube Studio:
//   1. The form: the fields chat will fill (name, position, experience, why we should hire you, ...), empty, each with
//      a key of its own; writing in them, made safe to show.
//   2. The paper: a picture VTube Studio takes (64 to 2048 pixels a side, under 5 MB as a PNG), its centre a third of
//      the way down the sheet, transparent above it; the same every time; ink where something is written in.
//   3. The ArtMesh it pins to, picked from Sarxina's model and from others riggers name differently.
//   4. The requests as VTube Studio's API takes them.
//   5. Up and down with a stand-in for VTube Studio: the order of the requests, the ArtMesh asked for once per model,
//      the permission box, menus that are open, toggles that come while it is busy, the paper taken away in VTube
//      Studio, no model, no manager.
//   6. The chat command.
// Given a folder, it also writes the picture VTube Studio gets, the flat sheet, and one filled in, as PNG files there.
// Inside OBS, in-obs-check.cpp goes on from here.
#include "self-test.hpp"

#include "chat-trigger.hpp"
#include "form.hpp"
#include "paper-prop.hpp"
#include "paper.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace sarxina;
using namespace sarxina::jobapplication;

namespace {

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

// Some of the ArtMeshes of Sarxina's model (Sarxina.moc3), the chest and body ones and those around them.
const QStringList kSarxina{
	"InnerOutfit_Back",        "ChestStar_BeadsTassels_Skinning", "InnerOutfit_Collarbone", "InnerOutfit_ChestJewel",
	"InnerOutfit_Chest",       "InnerOutfit_Waist",               "NeckPiece_TopChain",     "R_NeckPiece_Main",
	"ChestStarGlow_Add",       "ChestStarMain",                   "ChestStar_Underfill",    "ChestSmallStar1",
	"ChestGold_Tassel",        "ChestTassel",                     "ChestStar_Multiply",     "ChestLosangeJewelMiddle",
	"ChestLosangeMiddle",      "ChestLosangeMiddleUnderfill",     "R_InnerOutfit_ChestLines", "L_InnerOutfit_Chest",
	"R_InnerOutfit_Chest",     "InnerOutfit_CorsetMiddle",        "NeckMain",               "R_Pecs",
	"L_Pecs",                  "BellyButton",                     "Abs",                    "Torso_Shade",
	"Torso_Main",              "Torso_Underfill",                 "Glue__Torso_Main__Torso_Underfill",
	"Glue__Hips__Torso_Main",  "HairFront_Main",                  "FaceMain",               "R_Arm_Main"};

// ---- 1. the form ----------------------------------------------------------------------------------------------------

bool form()
{
	Tally tally;
	Form blank = jobApplication();
	const std::vector<std::string> keys = blank.keys();
	const std::set<std::string> unique(keys.begin(), keys.end());
	tally.expect(unique.size() == keys.size(), "every field has its own key");
	for (const char *named : {"name", "position", "experience", "why_hire"})
		tally.expect(blank.field(named) != nullptr, std::string("a field for ") + named);
	bool empty = true, labelled = true;
	for (const Section &section : blank.sections) {
		labelled = labelled && !section.heading.isEmpty();
		for (const Row &row : section.rows)
			for (const Field &field : row.fields) {
				empty = empty && field.value.isEmpty();
				if (field.kind != Field::Kind::Lines)
					labelled = labelled && !field.label.isEmpty();
			}
	}
	tally.expect(empty, "every field empty");
	tally.expect(labelled && !blank.title.isEmpty(), "every heading and label has its words");

	Form filled = blank;
	tally.expect(filled.set("name", QStringLiteral("Sarxina")) && filled.field("name")->value == "Sarxina",
		     "a name written in");
	tally.expect(filled.set("position", QChar(0x202E) + QStringLiteral("  Professional   Elf ")) &&
			     filled.field("position")->value == "Professional Elf",
		     "text made safe: no turning characters, spaces tidied");
	filled.set("why_hire", QString(500, QLatin1Char('a')));
	tally.expect(filled.field("why_hire")->value.size() == longestValue(*filled.field("why_hire")),
		     "a long answer cut to what fits");
	tally.expect(filled.set("hours", QStringLiteral("part TIME")) && filled.field("hours")->value == "Part time",
		     "a choice ticked, in any case");
	tally.expect(!filled.set("hours", QStringLiteral("whenever")) && filled.field("hours")->value == "Part time",
		     "not a choice: nothing ticked");
	tally.expect(!filled.set("salary_expectations", QStringLiteral("a lot")), "no such field");
	filled.clear();
	tally.expect(filled.field("name")->value.isEmpty() && filled.field("hours")->value.isEmpty(), "cleared");
	return tally.report("the form", "checks as expected");
}

// ---- 2. the paper ---------------------------------------------------------------------------------------------------

int alphaAt(const QImage &image, double x, double y)
{
	return qAlpha(image.pixel(static_cast<int>(x), static_cast<int>(y)));
}

// How much of a rectangle is ink: pixels well darker than paper.
double inked(const QImage &image, const QRectF &area)
{
	int dark = 0, all = 0;
	for (int y = static_cast<int>(area.top()); y < static_cast<int>(area.bottom()); y += 2)
		for (int x = static_cast<int>(area.left()); x < static_cast<int>(area.right()); x += 2) {
			all++;
			dark += qGray(image.pixel(x, y)) < 150;
		}
	return all ? static_cast<double>(dark) / all : 0;
}

Form filledIn()
{
	Form form = jobApplication();
	form.set("name", QStringLiteral("Sarxina"));
	form.set("date", QStringLiteral("10/31"));
	form.set("position", QStringLiteral("Professional Elf"));
	form.set("pay", QStringLiteral("$ 3 gold coins"));
	form.set("start", QStringLiteral("Never"));
	form.set("hours", QStringLiteral("Seasonal"));
	form.set("experience", QStringLiteral("Librarian of the astral archive for 800 years. Once alphabetised a "
					      "dragon. Can read in the dark"));
	form.set("why_hire", QStringLiteral("I have glasses so I look smart and my chat said I should apply"));
	form.set("signature", QStringLiteral("Sarxina"));
	return form;
}

bool paper(QImage &picture, QByteArray &file, double &paintMs)
{
	Tally tally;
	const QSize size = pictureSize();
	tally.expect(size.width() >= 64 && size.height() >= 64 && size.width() <= kLargestSide &&
			     size.height() <= kLargestSide,
		     "a size VTube Studio takes");

	QElapsedTimer took;
	took.start();
	picture = paintPicture(jobApplication(), 1026);
	file = png(picture);
	paintMs = static_cast<double>(took.elapsed());
	tally.expect(picture.size() == size, "painted at that size");
	tally.expect(file.size() < kLargestPicture * 4 / 5, "the PNG well under VTube Studio's 5 MB");

	const QRectF sheet = sheetInPicture();
	const double middle = size.width() / 2.0;
	tally.expect(std::abs(size.height() / 2.0 - (sheet.top() + kPinDown * sheet.height())) < 1,
		     "the picture's centre a third of the way down the sheet");
	tally.expect(alphaAt(picture, middle, size.height() / 2.0) == 255, "the sheet at the picture's centre");
	tally.expect(alphaAt(picture, middle, 10) == 0 && alphaAt(picture, middle, sheet.top() - 30) == 0,
		     "nothing above the sheet: that is the face");
	tally.expect(alphaAt(picture, middle, sheet.bottom() - 6) == 255 && alphaAt(picture, sheet.left() + 24, sheet.top() + 30) == 255,
		     "the sheet down to its bottom and out to its sides");
	tally.expect(alphaAt(picture, 4, 4) == 0 && alphaAt(picture, size.width() - 3, size.height() - 3) == 0,
		     "the corners clear");

	tally.expect(png(paintPicture(jobApplication(), 1026)) == file, "the same picture every time");
	tally.expect(paintSheet(jobApplication(), 7) != paintSheet(jobApplication(), 8), "another seed, another grain");

	// The name's line, in the flat sheet: blank, then written in.
	const QImage blankSheet = paintSheet(jobApplication(), 1026), filledSheet = paintSheet(filledIn(), 1026);
	const QRectF nameLine(220, 215, 300, 46);
	tally.expect(inked(filledSheet, nameLine) > inked(blankSheet, nameLine) + 0.01, "ink where the name is written");
	return tally.report("the paper", "checks as expected");
}

// ---- 3. what it pins to ---------------------------------------------------------------------------------------------

bool pins()
{
	Tally tally;
	tally.expect(pinTarget(kSarxina) == "ChestLosangeMiddle", "Sarxina: the diamond in the middle of the chest");
	QStringList noDiamond = kSarxina;
	noDiamond.removeAll("ChestLosangeMiddle");
	noDiamond.removeAll("ChestLosangeJewelMiddle");
	noDiamond.removeAll("ChestLosangeMiddleUnderfill");
	tally.expect(pinTarget(noDiamond) == "Torso_Main", "Sarxina without it: the torso");
	tally.expect(pinTarget({"ArtMesh1", "ArtMesh2", "ArtMesh3"}).isEmpty(), "unnamed ArtMeshes: none");
	tally.expect(pinTarget({"Face", "Hair_Front", "Body"}) == "Body", "a body");
	tally.expect(pinTarget({"chest_shadow", "Chest", "ChestLight"}) == "Chest", "a chest, not its shadow or light");
	tally.expect(pinTarget({"L_Chest", "Torso"}) == "Torso", "the torso, not one side of the chest");
	tally.expect(pinTarget({"UpperBody_Main", "BodyShade", "Arm_Body"}) == "UpperBody_Main", "the upper body");
	tally.expect(pinTarget({"Body2", "Body1"}) == "Body1", "a tie: the first by name");
	return tally.report("pinning", "checks as expected");
}

// ---- 4. the requests ------------------------------------------------------------------------------------------------

bool requests(const QByteArray &file)
{
	Tally tally;
	const QJsonObject load = loadRequest(file, 1.4, -3, 0.1, -0.2);
	const QString name = load.value("fileName").toString();
	static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9-]+\\.png$"));
	tally.expect(name.size() >= 8 && name.size() <= 32 && allowed.match(name).hasMatch(),
		     "a file name VTube Studio takes");
	tally.expect(QByteArray::fromBase64(load.value("customDataBase64").toString().toLatin1()) == file,
		     "the picture as base64");
	tally.expect(load.value("size").toDouble() == 1.0, "the size held to 0 to 1");
	tally.expect(load.value("order").toInt() > 0, "in front of the model");
	tally.expect(!load.value("customDataAskUserFirst").toBool(true), "no box for each picture");
	tally.expect(load.value("unloadWhenPluginDisconnects").toBool(), "gone when the connection goes");
	tally.expect(load.value("fadeTime").toDouble() > 0 && load.value("fadeTime").toDouble() <= 2, "fading in");

	const QJsonObject pin = pinRequest("item-1", "model-1", "ChestLosangeMiddle", 0.9, 2.5);
	const QJsonObject info = pin.value("pinInfo").toObject();
	tally.expect(pin.value("pin").toBool() && pin.value("itemInstanceID").toString() == "item-1", "pins that item");
	tally.expect(pin.value("vertexPinType").toString() == "Center" &&
			     pin.value("angleRelativeTo").toString() == "RelativeToModel" &&
			     pin.value("sizeRelativeTo").toString() == "RelativeToWorld",
		     "to the middle of the ArtMesh, tilted against the model, sized against the world");
	tally.expect(info.value("artMeshID").toString() == "ChestLosangeMiddle" && info.value("modelID").toString() == "model-1" &&
			     info.value("angle").toDouble() == 2.5 && info.value("size").toDouble() == 0.9,
		     "its ArtMesh, model, tilt and size");
	const double weights = info.value("vertexWeight1").toDouble() + info.value("vertexWeight2").toDouble() +
			       info.value("vertexWeight3").toDouble();
	tally.expect(std::abs(weights - 1) < 1e-9, "weights adding up to 1");

	const QJsonObject unload = unloadRequest("item-1");
	tally.expect(unload.value("instanceIDs").toArray() == QJsonArray{"item-1"} &&
			     !unload.value("unloadAllLoadedByThisPlugin").toBool(true) &&
			     !unload.value("unloadAllInScene").toBool(true),
		     "takes down that item only, not other plugins' items");
	tally.expect(permissionRequest().value("requestedPermission").toString() == "LoadCustomImagesAsItems",
		     "the permission for custom images");
	return tally.report("requests", "checks as expected");
}

// ---- 5. up and down -------------------------------------------------------------------------------------------------

// Answers like VTube Studio, later rather than at once, as the manager does.
struct StandIn {
	QStringList sent;
	bool reachable = true;
	bool modelLoaded = true;
	QStringList meshes = kSarxina;
	bool needsPermission = false;
	bool permissionGiven = false;
	bool boxAnswers = true; // false: the box waits longer than the manager does
	int menusOpen = 0;      // loads refused this many more times
	std::set<QString> items;
	int next = 0;
	double lastSize = -1;

	PaperProp::Send send()
	{
		return [this](const QString &type, const QJsonObject &data, PaperProp::Answer answer) {
			if (!reachable)
				return false;
			sent << type;
			bool ok = true;
			const QJsonObject reply = this->reply(type, data, ok);
			QTimer::singleShot(0, [answer, ok, reply] { answer(ok, reply); });
			return true;
		};
	}

	static QJsonObject error(int id, const QString &message)
	{
		return {{"errorID", id}, {"message", message}};
	}

	QJsonObject reply(const QString &type, const QJsonObject &data, bool &ok)
	{
		if (type == "CurrentModelRequest")
			return {{"modelLoaded", modelLoaded},
				{"modelName", "Sarxina"},
				{"modelID", "sarxina-model"},
				{"modelPosition", QJsonObject{{"positionX", 0.05}, {"positionY", -0.3}, {"rotation", 0}, {"size", -20}}}};
		if (type == "ArtMeshListRequest") {
			QJsonArray names;
			for (const QString &mesh : meshes)
				names << mesh;
			return {{"modelLoaded", true}, {"numberOfArtMeshNames", names.size()}, {"artMeshNames", names}};
		}
		if (type == "PermissionRequest") {
			if (!boxAnswers) {
				ok = false;
				return {{"message", "VTube Studio did not answer in time"}};
			}
			permissionGiven = true;
			return {{"grantSuccess", true}, {"requestedPermission", "LoadCustomImagesAsItems"}};
		}
		if (type == "ItemLoadRequest") {
			if (needsPermission && !permissionGiven) {
				ok = false;
				return error(9, "This request requires the LoadCustomImagesAsItems permission");
			}
			if (menusOpen > 0) {
				menusOpen--;
				ok = false;
				return error(753, "Cannot load items right now");
			}
			lastSize = data.value("size").toDouble();
			const QString id = QStringLiteral("item-%1").arg(++next);
			items.insert(id);
			return {{"instanceID", id}, {"fileName", data.value("fileName")}};
		}
		if (type == "ItemPinRequest")
			return {{"isPinned", true}, {"itemInstanceID", data.value("itemInstanceID")}};
		if (type == "ItemUnloadRequest") {
			for (const QJsonValue &id : data.value("instanceIDs").toArray())
				items.erase(id.toString());
			return {{"unloadedItems", QJsonArray{}}};
		}
		if (type == "ItemListRequest") {
			QJsonArray inScene;
			for (const QString &id : items)
				inScene << QJsonObject{{"instanceID", id}};
			return {{"itemInstancesInScene", inScene}};
		}
		ok = false;
		return error(1, "unknown request");
	}
};

// Lets the answers and retries come, until the prop is done.
void settle(PaperProp &prop)
{
	QElapsedTimer waited;
	waited.start();
	do
		QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
	while (prop.busy() && waited.elapsed() < 3000);
	QCoreApplication::processEvents();
}

QString joined(const QStringList &list)
{
	QStringList shorter;
	for (const QString &type : list)
		shorter << QString(type).remove(QStringLiteral("Request"));
	return shorter.join(QStringLiteral(", "));
}

bool upAndDown()
{
	Tally tally;
	const QByteArray picture = png(QImage(64, 64, QImage::Format_ARGB32));
	const auto make = [&picture](StandIn &vts) {
		auto prop = std::make_unique<PaperProp>(vts.send());
		prop->setPicture(picture);
		prop->setSize(0.9);
		prop->retryMs = 5;
		prop->tilt = [] { return 2.0; };
		return prop;
	};

	{
		StandIn vts;
		auto prop = make(vts);
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && prop->pinnedTo() == "ChestLosangeMiddle", "up, pinned to the chest");
		tally.expect(joined(vts.sent) == "CurrentModel, ArtMeshList, ItemLoad, ItemPin",
			     "asked in order: " + joined(vts.sent).toStdString());
		tally.expect(vts.lastSize == 0.9, "at its size");
		vts.sent.clear();
		prop->check();
		settle(*prop);
		tally.expect(prop->shown() && joined(vts.sent) == "ItemList", "still there when looked for");
		vts.sent.clear();
		prop->toggle();
		settle(*prop);
		tally.expect(!prop->shown() && vts.items.empty() && joined(vts.sent) == "ItemUnload", "down again");
		vts.sent.clear();
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && joined(vts.sent) == "CurrentModel, ItemLoad, ItemPin",
			     "up again, the ArtMesh remembered: " + joined(vts.sent).toStdString());

		// Taken away in VTube Studio: the toggle brings it up again, not down.
		vts.items.clear();
		prop->check();
		settle(*prop);
		tally.expect(!prop->shown() && !prop->wanted(), "taken away in VTube Studio, noticed");
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && vts.items.size() == 1, "and the next toggle brings it up");
	}
	{
		StandIn vts;
		vts.needsPermission = true;
		auto prop = make(vts);
		prop->show();
		settle(*prop);
		tally.expect(prop->shown() && joined(vts.sent) == "CurrentModel, ArtMeshList, ItemLoad, Permission, ItemLoad, ItemPin",
			     "asks for the permission once, then loads: " + joined(vts.sent).toStdString());
	}
	{
		StandIn vts;
		vts.needsPermission = true;
		vts.boxAnswers = false;
		auto prop = make(vts);
		prop->show();
		QElapsedTimer waited;
		waited.start();
		while (waited.elapsed() < 40) // the box is up; loads are tried again meanwhile
			QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
		const int boxes = vts.sent.count("PermissionRequest");
		vts.permissionGiven = true; // the streamer clicks Allow
		settle(*prop);
		tally.expect(prop->shown() && boxes == 1 && vts.sent.count("PermissionRequest") == 1,
			     "waits for the streamer to allow it, without asking twice");
	}
	{
		StandIn vts;
		vts.menusOpen = 3;
		auto prop = make(vts);
		prop->show();
		settle(*prop);
		tally.expect(prop->shown() && vts.sent.count("ItemLoadRequest") == 4, "loads once VTube Studio's menus close");
	}
	{
		StandIn vts;
		vts.menusOpen = 1000;
		auto prop = make(vts);
		prop->retries = 5;
		prop->show();
		settle(*prop);
		tally.expect(!prop->shown() && !prop->wanted() && !prop->busy(), "gives up when they never close");
	}
	{
		StandIn vts;
		auto prop = make(vts);
		prop->toggle();
		prop->toggle(); // taken back at once
		settle(*prop);
		tally.expect(!prop->shown() && vts.items.empty(), "up and straight back down: down, nothing left");
		vts.sent.clear();
		prop->toggle();
		prop->toggle();
		prop->toggle(); // three: up
		settle(*prop);
		tally.expect(prop->shown() && vts.items.size() == 1, "three toggles at once: up, once");
	}
	{
		StandIn vts;
		vts.modelLoaded = false;
		auto prop = make(vts);
		prop->toggle();
		settle(*prop);
		tally.expect(!prop->shown() && !prop->wanted() && joined(vts.sent) == "CurrentModel", "no model: nothing");
	}
	{
		StandIn vts;
		vts.meshes = {"ArtMesh1", "ArtMesh2"};
		auto prop = make(vts);
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && prop->pinnedTo().isEmpty() && !vts.sent.contains("ItemPinRequest"),
			     "no chest to be found: up, not pinned");
	}
	{
		StandIn vts;
		vts.reachable = false;
		auto prop = make(vts);
		prop->toggle();
		settle(*prop);
		tally.expect(!prop->shown() && !prop->wanted() && !prop->busy(), "no manager: nothing");
	}
	{
		StandIn vts;
		auto prop = make(vts);
		prop->toggle();
		prop->stop(); // OBS closing while it comes up
		settle(*prop);
		tally.expect(!prop->busy() && vts.sent.size() == 1, "stopped: answers dropped, nothing more sent");
	}
	return tally.report("up and down", "checks as expected");
}

// ---- 6. chat --------------------------------------------------------------------------------------------------------

bool chat()
{
	Tally tally;
	ChatTrigger trigger("apply");
	sarxina_chat_message message{};
	message.display_name = "Viewer";
	message.user_name = "viewer";
	message.reward_id = "";
	QString who;
	message.text = "!apply";
	tally.expect(trigger.hear(message, 0, true, who) && who == "Viewer", "!apply");
	tally.expect(!trigger.hear(message, 4000, true, who), "again 4 s later, cooling down");
	message.text = "!Apply now";
	tally.expect(trigger.hear(message, 10000, true, who), "once cooled down");
	message.text = "!apply";
	tally.expect(!trigger.hear(message, 30000, false, who), "not while it cannot go");
	return tally.report("chat", "checks as expected");
}

void pictures(const char *folder, const QImage &picture, const QByteArray &file)
{
	if (!folder || !*folder)
		return;
	QDir().mkpath(QString::fromUtf8(folder));
	const QDir dir(QString::fromUtf8(folder));
	int written = 0;
	QFile vts(dir.filePath("jobapplication-picture.png"));
	if (vts.open(QIODevice::WriteOnly) && vts.write(file) == file.size())
		written++;
	written += paintSheet(jobApplication(), 1026).save(dir.filePath("jobapplication-sheet.png"));
	written += paintPicture(filledIn(), 1026).save(dir.filePath("jobapplication-filled.png"));
	test::report(written == 3, "wrote %d pictures to %s (jobapplication-picture.png, %d x %d, is what VTube Studio gets)",
		     written, folder, picture.width(), picture.height());
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	QImage picture;
	QByteArray file;
	double paintMs = 0;
	bool passed = form();
	passed = paper(picture, file, paintMs) && passed;
	test::report(true, "the paper: a %d x %d picture, %lld KB as a PNG, painted and packed in %.0f ms", picture.width(),
		     picture.height(), static_cast<long long>(file.size() / 1024), paintMs);
	passed = pins() && passed;
	passed = requests(file) && passed;
	passed = upAndDown() && passed;
	passed = chat() && passed;
	pictures(picturesFolder, picture, file);
	test::report(passed, "%s", passed ? "passed" : "FAILED");
	return passed;
}
