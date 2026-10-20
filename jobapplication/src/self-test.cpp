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
//   7. The paper as a Live2D item: its .moc3 read back; how it bends (held at the top, turned about it, curled toward
//      the camera or back, twisted, never stretched); its texture; its physics and VTube Studio settings; its folder
//      written into and taken out of an Items folder; and up and down that way with the stand-in for VTube Studio,
//      and the picture whenever it cannot be.
// Given a folder, it also writes the picture VTube Studio gets, the flat sheet, and one filled in, as PNG files there.
// Inside OBS, in-obs-check.cpp goes on from here.
#include "self-test.hpp"

#include "chat-trigger.hpp"
#include "form.hpp"
#include "moc3.hpp"
#include "paper-item.hpp"
#include "paper-prop.hpp"
#include "paper.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
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
	// Live2D items: the Items folder VTube Studio reads (none: VTube Studio is not found), whether it sees what is put
	// there, and an error it answers loading one with (0: none).
	QString itemsFolder;
	bool seesItems = true;
	int refusesItems = 0;
	QJsonObject lastLoad;

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
		if (type == "VTSFolderInfoRequest")
			return {{"models", "Live2DModels"}, {"items", "Items"}, {"backgrounds", "Backgrounds"}};
		if (type == "ItemLoadRequest" && data.value("customDataBase64").toString().isEmpty()) {
			lastLoad = data;
			const QString name = data.value("fileName").toString();
			if (!seesItems || itemsFolder.isEmpty() || !QDir(itemsFolder).exists(name)) {
				ok = false;
				return error(751, "Item file not found");
			}
			if (refusesItems) {
				ok = false;
				return error(refusesItems, "This item cannot be loaded");
			}
			if (menusOpen > 0) {
				menusOpen--;
				ok = false;
				return error(753, "Cannot load items right now");
			}
			lastSize = data.value("size").toDouble();
			const QString id = QStringLiteral("item-%1").arg(++next);
			items.insert(id);
			return {{"instanceID", id}, {"fileName", name}};
		}
		if (type == "ItemLoadRequest") {
			lastLoad = data;
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
			QJsonArray inScene, files;
			for (const QString &id : items)
				inScene << QJsonObject{{"instanceID", id}};
			if (data.value("includeAvailableItemFiles").toBool() && seesItems && !itemsFolder.isEmpty())
				for (const QString &folder : QDir(itemsFolder).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
					if (QDir(QDir(itemsFolder).filePath(folder)).entryList({"*.model3.json"}).size() == 1)
						files << QJsonObject{{"fileName", folder}, {"type", "Live2D"}, {"loadedCount", 0}};
			return {{"itemInstancesInScene", inScene}, {"availableItemFiles", files}};
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
		prop->live2d = false; // these are the picture's own ways; the Live2D item's are in live2dUpAndDown()
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

// ---- 7. the Live2D item ---------------------------------------------------------------------------------------------

double distance(const QPointF &a, const QPointF &b)
{
	return std::hypot(a.x() - b.x(), a.y() - b.y());
}

// The sheet's middle line from top to bottom in 3D, put back together from where place() puts it and how near it
// came: its length, which paper keeps whatever it does.
double middleLength(const PaperPose &pose)
{
	double length = 0, lastY = 0, lastZ = 0;
	for (int i = 0; i <= 400; i++) {
		double depth = 0;
		const QPointF at = place(0, kSheetHeight * i / 400.0, pose, &depth);
		const double closer = 2.6 * kSheetHeight / (2.6 * kSheetHeight - depth);
		const double y = (at.y() + kPinDown * kSheetHeight) / closer;
		if (i)
			length += std::hypot(y - lastY, depth - lastZ);
		lastY = y;
		lastZ = depth;
	}
	return length;
}

QJsonObject jsonFile(const PaperItem &item, const QString &suffix)
{
	for (const auto &[path, bytes] : item.files)
		if (path.endsWith(suffix))
			return QJsonDocument::fromJson(bytes).object();
	return {};
}

bool item(double &paintMs, QByteArray &texture)
{
	Tally tally;

	// The .moc3, as it reads back.
	const QByteArray moc = paperMoc();
	const moc3::Summary read = moc3::read(moc);
	tally.expect(read.ok, "the .moc3 reads back" + (read.ok ? std::string() : ": " + read.why.toStdString()));
	tally.expect(read.version == moc3::kVersion300 && moc.size() % 64 == 0 && moc.size() < 8 * 1024 * 1024,
		     "version 3.00, a multiple of 64 bytes, under 8 MB");
	const QStringList meshes{"PaperShadow", "PaperSheet", "PaperShadeDark", "PaperShadeLight"};
	tally.expect(read.meshes == meshes, "its meshes, back to front: shadow, sheet, two shadings");
	QStringList parameters;
	for (const char *id : kTrackedParameters)
		parameters << id;
	parameters << kSwing << kCurl << kTwist << kRipple;
	tally.expect(read.parameters == parameters, "its parameters: six tracked, four that bend it");
	tally.expect(std::all_of(read.keyformCounts.begin(), read.keyformCounts.end(), [](int n) { return n == 7 * 5 * 5 * 3; }),
		     "a keyform for every combination of the four's keys");
	const QSize canvas = pictureSize();
	tally.expect(read.canvas[3] == canvas.width() && read.canvas[4] == canvas.height() &&
			     read.canvas[1] == canvas.width() / 2.0f && read.canvas[2] == canvas.height() / 2.0f,
		     "the picture's size, its origin in the middle, where the picture's centre is pinned");
	bool uvsInside = true;
	for (const auto &uvs : read.uvs)
		for (float uv : uvs)
			uvsInside = uvsInside && uv >= 0 && uv <= 1;
	tally.expect(uvsInside, "every UV on the texture");
	tally.expect(moc3::write(moc3::Model{}).isEmpty(), "a model with nothing in it is not written");

	// How it bends.
	const double H = kSheetHeight, W = kSheetWidth, degree = 3.14159265358979323846 / 180;
	tally.expect(distance(place(0, kPinDown * H, {}), {0, 0}) < 1e-6 &&
			     distance(place(W / 2, H, {}), {W / 2, (1 - kPinDown) * H}) < 1e-6,
		     "at rest, flat, the pin a third of the way down");
	tally.expect(distance(place(0, H, {30, 0, 0, 0}), {-H * std::sin(30 * degree), H * std::cos(30 * degree) - kPinDown * H}) < 1e-6,
		     "a swing turns it about the middle of its top edge, clockwise");
	tally.expect(distance(place(-W / 2, 0, {0, 1, 1, 1}), place(-W / 2, 0, {})) < 1e-6 &&
			     distance(place(W / 2, 0.05 * H, {0, -1, -1, -1}), place(W / 2, 0.05 * H, {})) < 1e-6,
		     "its top edge stays as held, whatever it does below");
	double toward = 0, away = 0;
	place(0, H, {0, 1, 0, 0}, &toward);
	place(0, H, {0, -1, 0, 0}, &away);
	tally.expect(toward > 0.3 * H && away < -0.3 * H, "a curl brings the bottom toward the camera, or back");
	tally.expect(place(0, H, {0, -1, 0, 0}).y() < place(0, H, {}).y() - 0.1 * H, "and so the sheet looks shorter");
	bool keepsLength = true;
	for (const PaperPose &pose : {PaperPose{0, 1, 0, 0}, PaperPose{0, -1, 0, 0}, PaperPose{0, 0.5, 0, 1},
				      PaperPose{0, -1, 0, -1}})
		keepsLength = keepsLength && std::abs(middleLength(pose) - H) < 0.002 * H;
	tally.expect(keepsLength, "bent any way, it is as long as it was: paper bends, it does not stretch");
	double right = 0, left = 0;
	place(W / 2, H, {0, 0, 1, 0}, &right);
	place(-W / 2, H, {0, 0, 1, 0}, &left);
	tally.expect(right > 0.3 * W && left < -0.3 * W, "a twist brings one bottom corner forward and the other back");
	bool finite = true;
	for (double swing : {-45.0, 45.0})
		for (double bend : {-1.0, 1.0}) {
			const QPointF p = place(W / 2, H, {swing, bend, bend, bend});
			finite = finite && std::isfinite(p.x()) && std::isfinite(p.y());
		}
	tally.expect(finite, "every extreme pose is somewhere");

	// The texture: the sheet at 1:1, its shadow and shadings beside it, nothing else.
	QElapsedTimer took;
	took.start();
	const QImage sheet = paintSheet(jobApplication(), 1026);
	const QImage image = paperTexture(sheet);
	texture = png(image);
	paintMs = static_cast<double>(took.elapsed());
	const QRectF at = sheetInTexture();
	tally.expect(image.width() == kTextureSide && image.height() == kTextureSide, "2048 pixels square");
	tally.expect(image.copy(at.toRect()) == sheet.convertToFormat(QImage::Format_ARGB32), "the sheet in it, unchanged");
	tally.expect(qAlpha(image.pixel(kTextureSide - 4, kTextureSide - 4)) == 0 && qAlpha(image.pixel(4, kTextureSide - 4)) == 0,
		     "transparent where nothing is");
	const QImage filled = paperTexture(paintSheet(filledIn(), 1026));
	const QRectF nameLine = QRectF(220, 215, 300, 46).translated(at.topLeft());
	tally.expect(inked(filled, nameLine) > inked(image, nameLine) + 0.01, "what is written in, on the texture");

	// The item's files.
	const PaperItem blank = paperItem(texture), again = paperItem(texture), written = paperItem(png(filled));
	static const QRegularExpression named(QStringLiteral("^SarxinaJobApplication-[0-9a-f]{10}$"));
	tally.expect(named.match(blank.name).hasMatch(), "a folder name of its own: " + blank.name.toStdString());
	tally.expect(blank.name == again.name && blank.name != written.name,
		     "the same name for the same writing, a new one for new writing");
	const QJsonObject model3 = jsonFile(blank, ".model3.json");
	const QJsonObject references = model3.value("FileReferences").toObject();
	const QStringList wanted{references.value("Moc").toString(), references.value("Physics").toString(),
				 references.value("DisplayInfo").toString(),
				 references.value("Textures").toArray().at(0).toString()};
	bool present = true;
	for (const QString &file : wanted)
		present = present && std::any_of(blank.files.begin(), blank.files.end(),
						 [&file](const auto &f) { return f.first == file && !f.second.isEmpty(); });
	tally.expect(present && model3.value("Version").toInt() == 3, "every file the model names is there");
	tally.expect(blank.files.back().first.endsWith(".vtube.json") &&
			     blank.files[blank.files.size() - 2].first.endsWith(".model3.json"),
		     "the files that make it an item written last");

	const QJsonObject physics = jsonFile(blank, ".physics3.json");
	const QJsonArray settings = physics.value("PhysicsSettings").toArray();
	int inputs = 0, outputs = 0, vertices = 0;
	bool known = true;
	QStringList driven;
	for (const QJsonValue &value : settings) {
		const QJsonObject setting = value.toObject();
		for (const QJsonValue &in : setting.value("Input").toArray())
			known = known && parameters.contains(in.toObject().value("Source").toObject().value("Id").toString());
		for (const QJsonValue &out : setting.value("Output").toArray()) {
			driven << out.toObject().value("Destination").toObject().value("Id").toString();
			known = known && out.toObject().value("VertexIndex").toInt() < setting.value("Vertices").toArray().size();
		}
		inputs += setting.value("Input").toArray().size();
		outputs += setting.value("Output").toArray().size();
		vertices += setting.value("Vertices").toArray().size();
	}
	const QJsonObject meta = physics.value("Meta").toObject();
	tally.expect(meta.value("PhysicsSettingCount").toInt() == settings.size() &&
			     meta.value("TotalInputCount").toInt() == inputs && meta.value("TotalOutputCount").toInt() == outputs &&
			     meta.value("VertexCount").toInt() == vertices,
		     "the physics' counts add up");
	tally.expect(known && driven == QStringList({kSwing, kCurl, kTwist, kRipple}),
		     "physics reads the item's own parameters and moves the four that bend it");

	const QJsonObject vtube = jsonFile(blank, ".vtube.json");
	QStringList mapped;
	for (const QJsonValue &m : vtube.value("ParameterSettings").toArray())
		mapped << m.toObject().value("Input").toString() + "->" + m.toObject().value("OutputLive2D").toString();
	tally.expect(mapped == QStringList({"FaceAngleX->ParamAngleX", "FaceAngleY->ParamAngleY", "FaceAngleZ->ParamAngleZ",
					    "FacePositionX->ParamBodyMoveX", "FacePositionY->ParamBodyMoveY",
					    "FacePositionZ->ParamBodyMoveZ"}),
		     "VTube Studio gives it the face's angles and movement");
	const QJsonObject vtsPhysics = vtube.value("PhysicsSettings").toObject();
	tally.expect(vtsPhysics.value("Use").toBool() && vtsPhysics.value("DraggingPhysicsStrength").toInt() > 0 &&
			     vtube.value("FileReferences").toObject().value("Model").toString() == "JobApplication.model3.json",
		     "physics on, dragging physics on, its model named");

	// In a folder, and out again.
	QTemporaryDir temp;
	const QString items = QDir(temp.path()).filePath("VTube Studio_Data/StreamingAssets/Items");
	QDir().mkpath(items);
	QString why;
	tally.expect(installItem(items, blank, why) && QFile::exists(QDir(items).filePath(blank.name + "/JobApplication.moc3")) &&
			     QFile::exists(QDir(items).filePath(blank.name + "/JobApplication.2048/texture_00.png")),
		     "written into the Items folder, texture and all");
	tally.expect(!installItem(QDir(temp.path()).filePath("nowhere"), blank, why), "not into a folder that is not there");
	QDir(items).mkpath("SarxinaJobApplication-notours1234");
	QDir(items).mkpath("SarxinaJobApplication-0123456789"); // the right shape of name, but not written by this plugin
	QDir(items).mkpath("Someone Else's Hat");
	installItem(items, written, why);
	tally.expect(removeOldItems(items, written.name) == 1 && !QDir(items).exists(blank.name) &&
			     QDir(items).exists(written.name) && QDir(items).exists("SarxinaJobApplication-notours1234") &&
			     QDir(items).exists("SarxinaJobApplication-0123456789") && QDir(items).exists("Someone Else's Hat"),
		     "its older items taken out, never anything else");
	QFile exe(QDir(temp.path()).filePath("VTube Studio.exe"));
	const bool made = exe.open(QIODevice::WriteOnly);
	exe.close();
	tally.expect(made && QDir(itemsFolderBeside(exe.fileName(), "Items")) == QDir(items) &&
			     itemsFolderBeside(exe.fileName(), "Elsewhere").isEmpty() &&
			     itemsFolderBeside(QDir(temp.path()).filePath("Other.exe"), "Items").isEmpty(),
		     "the Items folder found beside VTube Studio");
	return tally.report("the Live2D item", "checks as expected");
}

bool live2dUpAndDown(const QByteArray &texture)
{
	Tally tally;
	const QByteArray picture = png(QImage(64, 64, QImage::Format_ARGB32));
	QTemporaryDir temp;
	const QString items = QDir(temp.path()).filePath("Items");
	const auto fresh = [&items] {
		QDir(items).removeRecursively();
		QDir().mkpath(items);
	};
	const auto make = [&](StandIn &vts) {
		auto prop = std::make_unique<PaperProp>(vts.send());
		prop->setPicture(picture);
		prop->setTexture(texture);
		prop->setSize(0.9);
		prop->retryMs = 5;
		prop->listRetryMs = 5;
		prop->tilt = [] { return 2.0; };
		prop->findItems = [&vts](const QString &name) { return name == "Items" ? vts.itemsFolder : QString(); };
		return prop;
	};

	{
		fresh();
		StandIn vts;
		vts.itemsFolder = items;
		auto prop = make(vts);
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && prop->bends() && prop->pinnedTo() == "ChestLosangeMiddle",
			     "up as the Live2D item, pinned to the chest");
		tally.expect(joined(vts.sent) == "CurrentModel, ArtMeshList, VTSFolderInfo, ItemList, ItemLoad, ItemPin",
			     "asked in order: " + joined(vts.sent).toStdString());
		tally.expect(vts.lastLoad.value("fileName").toString() == prop->itemName() &&
				     vts.lastLoad.value("customDataBase64").toString().isEmpty() && vts.lastSize == 0.9 &&
				     QDir(items).exists(prop->itemName()),
			     "loaded by the name of the folder written, nothing sent with it, at its size");
		vts.sent.clear();
		prop->toggle();
		settle(*prop);
		tally.expect(!prop->shown() && !prop->bends() && joined(vts.sent) == "ItemUnload", "down again");
		vts.sent.clear();
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && prop->bends() && joined(vts.sent) == "CurrentModel, ItemList, ItemLoad, ItemPin",
			     "up again, the folder's name remembered: " + joined(vts.sent).toStdString());

		// The streamer changed the item's settings in VTube Studio, which keeps them in its vtube.json; OBS restarts.
		const QString settings = QDir(items).filePath(prop->itemName() + "/JobApplication.vtube.json");
		QFile changed(settings);
		const bool wrote = changed.open(QIODevice::WriteOnly) && changed.write("{\"changed\": true}") > 0;
		changed.close();
		prop->toggle();
		settle(*prop);
		StandIn later;
		later.itemsFolder = items;
		auto restarted = make(later);
		restarted->toggle();
		settle(*restarted);
		QByteArray kept;
		if (QFile file(settings); file.open(QIODevice::ReadOnly))
			kept = file.readAll(); // and closed, so that the folder can go later
		tally.expect(wrote && restarted->bends() && kept.contains("changed"),
			     "after a restart, the item VTube Studio has is used as it is, its settings kept");
		restarted->toggle();
		settle(*restarted);
		prop->toggle();
		settle(*prop);

		// New writing: a new item; once it is up, the old one is gone from the folder.
		const QString before = prop->itemName();
		prop->toggle();
		settle(*prop);
		prop->setTexture(png(paperTexture(paintSheet(filledIn(), 1026))));
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && prop->bends() && prop->itemName() != before &&
				     QDir(items).exists(prop->itemName()) && !QDir(items).exists(before),
			     "new writing: a new item, the old one taken out of the folder");
	}
	{
		StandIn vts; // no VTube Studio to be found where it runs
		auto prop = make(vts);
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && !prop->bends() && !vts.lastLoad.value("customDataBase64").toString().isEmpty() &&
				     joined(vts.sent) == "CurrentModel, ArtMeshList, VTSFolderInfo, ItemLoad, ItemPin",
			     "no Items folder: up as the picture: " + joined(vts.sent).toStdString());
	}
	{
		fresh();
		StandIn vts;
		vts.itemsFolder = items;
		vts.seesItems = false; // it never lists the item
		auto prop = make(vts);
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && !prop->bends() && vts.sent.count("ItemListRequest") == prop->listTries,
			     "VTube Studio does not see the item: asked a few times, then the picture");
	}
	{
		fresh();
		StandIn vts;
		vts.itemsFolder = items;
		vts.refusesItems = 757;
		auto prop = make(vts);
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && !prop->bends(), "VTube Studio will not load it: the picture");
		prop->toggle();
		settle(*prop);
		vts.sent.clear();
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && !prop->bends() && joined(vts.sent) == "CurrentModel, ItemLoad, ItemPin",
			     "and from then on the picture straight away: " + joined(vts.sent).toStdString());
	}
	{
		fresh();
		StandIn vts;
		vts.itemsFolder = items;
		vts.menusOpen = 2;
		auto prop = make(vts);
		prop->show();
		settle(*prop);
		tally.expect(prop->shown() && prop->bends() && vts.sent.count("ItemLoadRequest") == 3,
			     "loads the item once VTube Studio's menus close");
	}
	{
		fresh();
		StandIn vts;
		vts.itemsFolder = items;
		auto prop = make(vts);
		prop->live2d = false;
		prop->toggle();
		settle(*prop);
		tally.expect(prop->shown() && !prop->bends() && QDir(items).isEmpty(), "told not to: the picture, nothing written");
	}
	return tally.report("up and down as a Live2D item", "checks as expected");
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

	// The Live2D item as the plugin installs it, blank and filled in, for the preview to load.
	for (const auto &[name, form] : {std::pair<const char *, Form>{"item-blank", jobApplication()},
					 std::pair<const char *, Form>{"item-filled", filledIn()}}) {
		QDir(dir.filePath(name)).removeRecursively();
		dir.mkpath(name);
		const PaperItem item = paperItem(png(paperTexture(paintSheet(form, 1026))));
		QString why;
		const bool ok = installItem(dir.filePath(name), item, why);
		test::report(ok, "wrote the Live2D item %s to %s: %s", item.name.toUtf8().constData(), name,
			     ok ? "OK" : why.toUtf8().constData());
	}
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
	double textureMs = 0;
	QByteArray texture;
	passed = item(textureMs, texture) && passed;
	test::report(true, "the Live2D item: a %d x %d texture, %lld KB as a PNG, painted and packed in %.0f ms; the .moc3 %lld KB",
		     kTextureSide, kTextureSide, static_cast<long long>(texture.size() / 1024), textureMs,
		     static_cast<long long>(paperMoc().size() / 1024));
	passed = live2dUpAndDown(texture) && passed;
	passed = chat() && passed;
	pictures(picturesFolder, picture, file);
	test::report(passed, "%s", passed ? "passed" : "FAILED");
	return passed;
}
