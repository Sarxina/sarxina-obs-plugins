#include "paper-prop.hpp"

#include "paper-item.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <set>

namespace sarxina::jobapplication {

namespace {

// VTube Studio's errorID for a request that needs a permission the plugin was not given, and for an item that cannot
// be loaded right now because the streamer has its menus open.
constexpr int kNeedsPermission = 9;
constexpr int kCannotLoadNow = 753;
constexpr int kNoSuchItem = 751;

// An ArtMesh ID read as lower-case words: "ChestLosangeMiddle" -> chest, losange, middle; "L_InnerOutfit_Chest2" ->
// l, inner, outfit, chest.
QStringList words(const QString &id)
{
	static const QRegularExpression boundary(QStringLiteral("(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])|[^A-Za-z]+"));
	QStringList out;
	for (const QString &word : id.split(boundary, Qt::SkipEmptyParts))
		out << word.toLower();
	return out;
}

int score(const QString &id)
{
	static const std::map<QString, int> body{{QStringLiteral("sternum"), 5}, {QStringLiteral("chest"), 4},
						 {QStringLiteral("torso"), 4},   {QStringLiteral("upperbody"), 4},
						 {QStringLiteral("body"), 3},    {QStringLiteral("breast"), 3},
						 {QStringLiteral("bust"), 3},    {QStringLiteral("shirt"), 2}};
	static const std::map<QString, int> where{{QStringLiteral("middle"), 3}, {QStringLiteral("center"), 3},
						  {QStringLiteral("centre"), 3}, {QStringLiteral("mid"), 3},
						  {QStringLiteral("main"), 1},   {QStringLiteral("base"), 1}};
	static const std::set<QString> against{
		QStringLiteral("shade"),     QStringLiteral("shadow"),   QStringLiteral("glow"),     QStringLiteral("add"),
		QStringLiteral("multiply"),  QStringLiteral("screen"),   QStringLiteral("opacity"),  QStringLiteral("underfill"),
		QStringLiteral("glue"),      QStringLiteral("clip"),     QStringLiteral("mask"),     QStringLiteral("line"),
		QStringLiteral("lines"),     QStringLiteral("outline"),  QStringLiteral("highlight"), QStringLiteral("light"),
		QStringLiteral("l"),         QStringLiteral("r"),        QStringLiteral("left"),     QStringLiteral("right"),
		QStringLiteral("back"),      QStringLiteral("star"),     QStringLiteral("stars"),    QStringLiteral("tassel"),
		QStringLiteral("bead"),      QStringLiteral("beads"),    QStringLiteral("bell"),     QStringLiteral("chain"),
		QStringLiteral("jewel"),     QStringLiteral("gold"),     QStringLiteral("hair"),     QStringLiteral("arm"),
		QStringLiteral("hand")};
	int total = 0;
	bool onBody = false;
	for (const QString &word : words(id)) {
		if (const auto found = body.find(word); found != body.end()) {
			total += found->second;
			onBody = true;
		} else if (const auto at = where.find(word); at != where.end()) {
			total += at->second;
		} else if (against.count(word)) {
			total -= 3;
		}
	}
	return onBody ? total : 0;
}

QString message(const QJsonObject &data)
{
	return data.value(QStringLiteral("message")).toString();
}

int errorId(const QJsonObject &data)
{
	return data.value(QStringLiteral("errorID")).toInt(-1);
}

// The manager's words for a connection to VTube Studio that is not there (sarxina-obs-plugin-manager: vtube-studio).
bool disconnected(const QJsonObject &data)
{
	const QString why = message(data);
	return errorId(data) < 0 && (why.startsWith(QStringLiteral("not connected")) ||
				     why.startsWith(QStringLiteral("lost the connection")) ||
				     why.contains(QStringLiteral("restarted")));
}

double randomTilt()
{
	const double degrees = 1.5 + 3.0 * QRandomGenerator::global()->generateDouble();
	return QRandomGenerator::global()->bounded(2) ? degrees : -degrees;
}

} // namespace

QString pinTarget(const QStringList &artMeshes)
{
	QString best;
	int bestScore = 0;
	for (const QString &id : artMeshes) {
		const int s = score(id);
		if (s <= 0)
			continue;
		const bool better = s > bestScore || (s == bestScore && (id.size() < best.size() ||
									(id.size() == best.size() && id < best)));
		if (better) {
			best = id;
			bestScore = s;
		}
	}
	return best;
}

// ---- the requests ---------------------------------------------------------------------------------------------------

QJsonObject loadRequest(const QByteArray &png, double size, double rotation, double x, double y, int order)
{
	return {
		{QStringLiteral("fileName"), QString::fromLatin1(kPictureName)},
		{QStringLiteral("positionX"), x},
		{QStringLiteral("positionY"), y},
		{QStringLiteral("size"), std::clamp(size, 0.0, 1.0)},
		{QStringLiteral("rotation"), rotation},
		{QStringLiteral("fadeTime"), kFadeSeconds},
		{QStringLiteral("order"), order},
		{QStringLiteral("failIfOrderTaken"), false},
		{QStringLiteral("smoothing"), 0},
		{QStringLiteral("censored"), false},
		{QStringLiteral("flipped"), false},
		{QStringLiteral("locked"), true},
		{QStringLiteral("unloadWhenPluginDisconnects"), true},
		{QStringLiteral("customDataBase64"), QString::fromLatin1(png.toBase64())},
		// Every picture with new writing on it is new to VTube Studio, so it must not ask about each one; the
		// permission (permissionRequest) is what the streamer is asked, once.
		{QStringLiteral("customDataAskUserFirst"), false},
		{QStringLiteral("customDataSkipAskingUserIfWhitelisted"), true},
		{QStringLiteral("customDataAskTimer"), -1},
	};
}

// The Live2D item, by the name of its folder: nothing of it is sent, VTube Studio reads it from its Items folder.
QJsonObject itemLoadRequest(const QString &item, double size, double rotation, double x, double y, int order)
{
	QJsonObject request = loadRequest({}, size, rotation, x, y, order);
	request.insert(QStringLiteral("fileName"), item);
	request.insert(QStringLiteral("customDataBase64"), QString());
	return request;
}

// The item files VTube Studio has (it reads its Items folder again for this), only that one if it has it.
QJsonObject itemFilesRequest(const QString &item)
{
	return {
		{QStringLiteral("includeAvailableSpots"), false},
		{QStringLiteral("includeItemInstancesInScene"), false},
		{QStringLiteral("includeAvailableItemFiles"), true},
		{QStringLiteral("onlyItemsWithFileName"), item},
		{QStringLiteral("onlyItemsWithInstanceID"), QString()},
	};
}

QJsonObject folderRequest()
{
	return {};
}

QJsonObject pinRequest(const QString &instance, const QString &model, const QString &artMesh, double size, double tilt)
{
	return {
		{QStringLiteral("pin"), true},
		{QStringLiteral("itemInstanceID"), instance},
		{QStringLiteral("angleRelativeTo"), QStringLiteral("RelativeToModel")},
		{QStringLiteral("sizeRelativeTo"), QStringLiteral("RelativeToWorld")},
		{QStringLiteral("vertexPinType"), QStringLiteral("Center")},
		{QStringLiteral("pinInfo"),
		 QJsonObject{
			 {QStringLiteral("modelID"), model},
			 {QStringLiteral("artMeshID"), artMesh},
			 {QStringLiteral("angle"), tilt},
			 {QStringLiteral("size"), std::clamp(size, 0.0, 1.0)},
			 {QStringLiteral("vertexID1"), 0},
			 {QStringLiteral("vertexID2"), 0},
			 {QStringLiteral("vertexID3"), 0},
			 {QStringLiteral("vertexWeight1"), 1},
			 {QStringLiteral("vertexWeight2"), 0},
			 {QStringLiteral("vertexWeight3"), 0},
		 }},
	};
}

QJsonObject unloadRequest(const QStringList &instances)
{
	QJsonArray ids;
	for (const QString &instance : instances)
		ids << instance;
	return {
		{QStringLiteral("unloadAllInScene"), false},
		{QStringLiteral("unloadAllLoadedByThisPlugin"), false}, // "this plugin" is the manager, for every plugin
		{QStringLiteral("allowUnloadingItemsLoadedByUserOrOtherPlugins"), false},
		{QStringLiteral("instanceIDs"), ids},
		{QStringLiteral("fileNames"), QJsonArray{}},
	};
}

QJsonObject unpinRequest(const QString &instance)
{
	return {{QStringLiteral("pin"), false}, {QStringLiteral("itemInstanceID"), instance}};
}

// -1000 leaves a value as it is.
QJsonObject moveAwayRequest(const QString &instance)
{
	return {{QStringLiteral("itemsToMove"),
		 QJsonArray{QJsonObject{
			 {QStringLiteral("itemInstanceID"), instance},
			 {QStringLiteral("timeInSeconds"), 0},
			 {QStringLiteral("fadeMode"), QStringLiteral("linear")},
			 {QStringLiteral("positionX"), 50},
			 {QStringLiteral("positionY"), 50},
			 {QStringLiteral("size"), -1000},
			 {QStringLiteral("rotation"), -1000},
			 {QStringLiteral("order"), -1000},
			 {QStringLiteral("setFlip"), false},
			 {QStringLiteral("flip"), false},
			 {QStringLiteral("userCanStop"), false},
		 }}}};
}

QJsonObject listRequest(const QString &instance)
{
	return {
		{QStringLiteral("includeAvailableSpots"), false},
		{QStringLiteral("includeItemInstancesInScene"), true},
		{QStringLiteral("includeAvailableItemFiles"), false},
		{QStringLiteral("onlyItemsWithFileName"), QString()},
		{QStringLiteral("onlyItemsWithInstanceID"), instance},
	};
}

QJsonObject permissionRequest()
{
	return {{QStringLiteral("requestedPermission"), QStringLiteral("LoadCustomImagesAsItems")}};
}

// ---- up and down ----------------------------------------------------------------------------------------------------

PaperProp::PaperProp(Send send) : tilt(randomTilt), findItems(findItemsFolder), send_(std::move(send)) {}

void PaperProp::toggle()
{
	wanted_ = !wanted_;
	go();
}

void PaperProp::show()
{
	wanted_ = true;
	go();
}

void PaperProp::hide()
{
	wanted_ = false;
	go();
}

void PaperProp::stop()
{
	stopped_ = true;
	generation_++;
	step_ = Step::Idle;
}

void PaperProp::rewrite()
{
	stale_ = true;
	go();
}

void PaperProp::go()
{
	if (stopped_ || busy())
		return;
	if (wanted_ && !shown()) {
		stale_ = false; // it comes up with the newest writing
		askModel();
	} else if (!wanted_ && shown()) {
		unload();
	} else if (wanted_ && shown() && stale_) {
		if (writing == showing_)
			stale_ = false; // it went up with the newest writing already
		else
			swap();
	}
}

// New writing for the paper that is up: the same model, ArtMesh and tilt, one place behind it.
void PaperProp::swap()
{
	stale_ = false;
	old_ = instance_;
	oldBends_ = bends_;
	behind_ = order_ > 1;
	order_ = behind_ ? order_ - 1 : kOrder;
	load();
}

void PaperProp::finish(bool)
{
	step_ = Step::Idle;
	go(); // a toggle may have come meanwhile
}

bool PaperProp::send(Step step, const char *type, const QJsonObject &data,
		     std::function<void(bool, const QJsonObject &)> then)
{
	step_ = step;
	const uint64_t generation = generation_;
	const bool sent = send_(QString::fromLatin1(type), data, [this, generation, then](bool ok, const QJsonObject &answer) {
		if (generation == generation_ && !stopped_)
			then(ok, answer);
	});
	if (!sent) {
		step_ = Step::Idle;
		wanted_ = false;
		say(QStringLiteral("cannot reach VTube Studio: the Sarxina OBS Plugin Manager is not installed"));
	}
	return sent;
}

void PaperProp::askModel()
{
	send(Step::Model, "CurrentModelRequest", {}, [this](bool ok, const QJsonObject &data) {
		if (!ok || !data.value(QStringLiteral("modelLoaded")).toBool()) {
			say(ok ? QStringLiteral("VTube Studio has no model loaded")
			       : QStringLiteral("VTube Studio: %1").arg(message(data)));
			wanted_ = false;
			finish(false);
			return;
		}
		model_ = data.value(QStringLiteral("modelID")).toString();
		const QJsonObject position = data.value(QStringLiteral("modelPosition")).toObject();
		modelX_ = position.value(QStringLiteral("positionX")).toDouble();
		modelY_ = position.value(QStringLiteral("positionY")).toDouble();
		if (pins_.count(model_))
			load();
		else
			askMeshes();
	});
}

void PaperProp::askMeshes()
{
	send(Step::Meshes, "ArtMeshListRequest", {}, [this](bool ok, const QJsonObject &data) {
		if (!ok) {
			say(QStringLiteral("VTube Studio: %1").arg(message(data)));
			wanted_ = false;
			finish(false);
			return;
		}
		QStringList meshes;
		for (const QJsonValue &mesh : data.value(QStringLiteral("artMeshNames")).toArray())
			meshes << mesh.toString();
		const QString pin = pinTarget(meshes);
		pins_[model_] = pin;
		say(pin.isEmpty() ? QStringLiteral("none of the model's %1 ArtMeshes looks like the chest or body: the "
						   "paper will stay where it loads, and not move with the model")
						.arg(meshes.size())
				  : QStringLiteral("the paper pins to %1 (of the model's %2 ArtMeshes)").arg(pin).arg(meshes.size()));
		load();
	});
}

void PaperProp::load()
{
	if (!hasPicture()) {
		say(QStringLiteral("no picture of the paper"));
		wanted_ = false;
		finish(false);
		return;
	}
	if (!wanted_) { // taken back while waiting to try again
		tries_ = 0;
		waitingForMenus_ = false;
		finish(true); // and down, with the paper it was to replace
		return;
	}
	loading_ = writing;
	if (old_.isEmpty()) {
		tilt_ = tilt ? tilt() : 0;
		order_ = kOrder;
	}
	if (live2d && !itemFailed_ && !texture_.isEmpty())
		loadItem();
	else
		loadPicture();
}

void PaperProp::loadItem()
{
	if (itemsName_.isEmpty()) {
		send(Step::Folder, "VTSFolderInfoRequest", folderRequest(), [this](bool ok, const QJsonObject &data) {
			itemsName_ = ok ? data.value(QStringLiteral("items")).toString() : QString();
			if (itemsName_.isEmpty())
				itemsName_ = QStringLiteral("Items");
			loadItem();
		});
		return;
	}
	const QString folder = findItems ? findItems(itemsName_) : QString();
	if (folder.isEmpty()) {
		fallBack(QStringLiteral("VTube Studio's %1 folder was not found").arg(itemsName_));
		return;
	}
	if (itemFor_.isEmpty() || itemFor_ != texture_) {
		item_ = paperItem(texture_);
		itemFor_ = texture_;
	}
	const PaperItem &item = item_;
	// The new writing into the item's folder (only what changed: the texture, usually), or the whole item the first
	// time. The streamer's settings for it stay as they are.
	const bool there = QFile::exists(QDir(folder).filePath(item.name + QStringLiteral("/") + QLatin1String(kItemBase) +
							       QStringLiteral(".vtube.json")));
	QString why;
	if (!installItem(folder, item, why)) {
		fallBack(why);
		return;
	}
	if (!there)
		say(QStringLiteral("the paper written to %1 as the Live2D item %2")
			    .arg(QDir::toNativeSeparators(folder), item.name));
	itemsFolder_ = folder;
	itemName_ = item.name;
	listed_ = 0;
	listItem();
}

void PaperProp::listItem()
{
	send(Step::Listing, "ItemListRequest", itemFilesRequest(itemName_), [this](bool ok, const QJsonObject &data) {
		bool seen = false;
		for (const QJsonValue &file : data.value(QStringLiteral("availableItemFiles")).toArray()) {
			const QJsonObject item = file.toObject();
			seen = seen || (item.value(QStringLiteral("fileName")).toString() == itemName_ &&
					item.value(QStringLiteral("type")).toString() == QStringLiteral("Live2D"));
		}
		if (seen) {
			send(Step::Load, "ItemLoadRequest", itemLoadRequest(itemName_, itemSize(), tilt_, modelX_, modelY_, order_),
			     [this](bool loaded, const QJsonObject &answer) { itemLoaded(loaded, answer); });
			return;
		}
		if (!ok && disconnected(data)) {
			fallBack(message(data)); // and that fails the same way, and says so
			return;
		}
		if (++listed_ < listTries) {
			step_ = Step::Listing;
			const uint64_t generation = generation_;
			QTimer::singleShot(listRetryMs, this, [this, generation] {
				if (generation == generation_ && !stopped_)
					wanted_ ? listItem() : finish(true);
			});
			return;
		}
		fallBack(ok ? QStringLiteral("VTube Studio does not list it in its items")
			    : QStringLiteral("VTube Studio: %1").arg(message(data)));
	});
}

void PaperProp::itemLoaded(bool ok, const QJsonObject &data)
{
	if (ok) {
		bends_ = true;
		const bool replacing = !old_.isEmpty();
		up(data);
		if (!replacing) // else once the paper it replaces is down
			if (const int removed = removeOldItems(itemsFolder_, itemName_))
				say(QStringLiteral("took %1 older paper item%2 out of the Items folder").arg(removed).arg(removed == 1 ? "" : "s"));
		return;
	}
	if (errorId(data) == kCannotLoadNow) {
		waitForMenus();
		return;
	}
	if (errorId(data) != kNoSuchItem)
		itemFailed_ = true; // it read the item and would not have it: no use asking again
	fallBack(QStringLiteral("VTube Studio did not load it: %1").arg(message(data)));
}

void PaperProp::fallBack(const QString &why)
{
	say(QStringLiteral("the paper as a Live2D item: %1. It comes up as a picture instead, which does not bend").arg(why));
	loadPicture();
}

void PaperProp::loadPicture()
{
	bends_ = false;
	send(Step::Load, "ItemLoadRequest", loadRequest(picture(), size_, tilt_, modelX_, modelY_, order_),
	     [this](bool ok, const QJsonObject &data) { loaded(ok, data); });
}

void PaperProp::loaded(bool ok, const QJsonObject &data)
{
	if (ok) {
		up(data);
		return;
	}
	const int error = errorId(data);
	if (error == kNeedsPermission) {
		if (askedPermission_) {
			retryLoad(QStringLiteral("waiting for the streamer to allow the Sarxina OBS Plugin Manager to load "
						 "custom images, in VTube Studio"));
			return;
		}
		askedPermission_ = true;
		say(QStringLiteral("asking VTube Studio for permission to load the paper as a custom image"));
		send(Step::Permission, "PermissionRequest", permissionRequest(), [this](bool granted, const QJsonObject &answer) {
			if (granted && answer.value(QStringLiteral("grantSuccess")).toBool()) {
				say(QStringLiteral("allowed to load custom images"));
				load();
			} else if (granted) {
				say(QStringLiteral("not allowed to load custom images: allow it in VTube Studio's plugin "
						   "settings, then toggle the paper again"));
				askedPermission_ = false;
				wanted_ = false;
				finish(false);
			} else {
				// The box waits for the streamer longer than the manager waits for an answer.
				retryLoad(QStringLiteral("waiting for the streamer to allow it in VTube Studio"));
			}
		});
		return;
	}
	if (error == kCannotLoadNow) {
		waitForMenus();
		return;
	}
	say(QStringLiteral("VTube Studio did not load the paper: %1").arg(message(data)));
	if (!old_.isEmpty()) { // the old paper stays up
		old_.clear();
		bends_ = oldBends_;
		finish(false);
		return;
	}
	wanted_ = false;
	finish(false);
}

// Loaded, the Live2D item or the picture: pinned, unless it was taken back meanwhile.
void PaperProp::up(const QJsonObject &data)
{
	tries_ = 0;
	if (waitingForMenus_)
		say(QStringLiteral("VTube Studio's menu or popup is closed"));
	waitingForMenus_ = false;
	const QString loaded = data.value(QStringLiteral("instanceID")).toString();
	if (loaded.isEmpty() && !old_.isEmpty()) {
		say(QStringLiteral("VTube Studio loaded the new writing but gave no instance ID; the paper stays as it was"));
		old_.clear();
		bends_ = oldBends_;
		finish(false);
		return;
	}
	instance_ = loaded;
	if (instance_.isEmpty()) {
		say(QStringLiteral("VTube Studio loaded the paper but gave no instance ID"));
		bends_ = false;
		wanted_ = false;
		finish(false);
	} else if (!wanted_) {
		unload(); // taken back while it loaded
	} else {
		pin();
	}
}

void PaperProp::retryLoad(const QString &why)
{
	if (tries_ == 0)
		say(why + QStringLiteral(" (trying again)"));
	if (++tries_ > retries) {
		say(QStringLiteral("gave up: ") + why);
		tries_ = 0;
		wanted_ = false;
		finish(false);
		return;
	}
	step_ = Step::Load;
	const uint64_t generation = generation_;
	QTimer::singleShot(retryMs, this, [this, generation] {
		if (generation == generation_ && !stopped_)
			load();
	});
}

// VTube Studio has a menu or a popup open. Tried again, gently, for as long as the paper is wanted; a toggle meanwhile
// takes it back (load() sees that).
void PaperProp::waitForMenus()
{
	if (!waitingForMenus_)
		say(QStringLiteral("VTube Studio has a menu or popup open, so it can't load the paper yet. It comes up as soon "
				   "as that's closed"));
	waitingForMenus_ = true;
	step_ = Step::Load;
	const uint64_t generation = generation_;
	QTimer::singleShot(menusRetryMs, this, [this, generation] {
		if (generation == generation_ && !stopped_)
			load();
	});
}

void PaperProp::pin()
{
	const QString artMesh = pins_[model_];
	if (artMesh.isEmpty()) {
		say(old_.isEmpty() ? QStringLiteral("the paper is up, not pinned") : QStringLiteral("new writing is up, not pinned"));
		if (!old_.isEmpty()) {
			swapped();
			return;
		}
		showing_ = loading_;
		if (wrote)
			wrote(loading_);
		finish(true);
		return;
	}
	send(Step::Pin, "ItemPinRequest", pinRequest(instance_, model_, artMesh, bends_ ? itemSize() : size_, tilt_),
	     [this, artMesh](bool ok, const QJsonObject &data) {
		     if (ok) {
			     pinnedTo_ = artMesh;
			     if (old_.isEmpty())
				     say(QStringLiteral("the paper is up%3, pinned to %1, tilted %2 degrees")
						 .arg(artMesh)
						 .arg(tilt_, 0, 'f', 1)
						 .arg(bends_ ? QStringLiteral(" as the Live2D item ") + itemName_
							     : QStringLiteral(" as a picture")));
		     } else {
			     say(QStringLiteral("the paper is up but VTube Studio did not pin it: %1").arg(message(data)));
		     }
		     if (!old_.isEmpty()) {
			     swapped();
			     return;
		     }
		     showing_ = loading_;
		     if (wrote)
			     wrote(loading_);
		     finish(true);
	     });
}

// The new writing is up and pinned: once it has settled behind the old paper (or faded in, in front of it), the old
// paper goes.
void PaperProp::swapped()
{
	step_ = Step::Unload;
	const uint64_t generation = generation_;
	QTimer::singleShot(behind_ ? settleMs : static_cast<int>(kFadeSeconds * 1000) + 150, this, [this, generation] {
		if (generation == generation_ && !stopped_)
			takeOldAway();
	});
}

// Off the picture first, so that VTube Studio's shrink as it unloads an item is not seen in front of the new paper;
// whatever VTube Studio answers, the old paper goes.
void PaperProp::takeOldAway()
{
	send(Step::Unload, "ItemPinRequest", unpinRequest(old_), [this](bool, const QJsonObject &) {
		send(Step::Unload, "ItemMoveRequest", moveAwayRequest(old_), [this](bool, const QJsonObject &) {
			send(Step::Unload, "ItemUnloadRequest", unloadRequest({old_}),
			     [this](bool ok, const QJsonObject &data) { oldGone(ok, data); });
		});
	});
}

void PaperProp::oldGone(bool ok, const QJsonObject &data)
{
	if (!ok)
		say(QStringLiteral("taking the old writing down: %1").arg(message(data)));
	old_.clear();
	say(QStringLiteral("new writing is up%1").arg(bends_ ? QStringLiteral(" as the Live2D item ") + itemName_
							     : QStringLiteral(" as a picture")));
	showing_ = loading_;
	if (wrote)
		wrote(loading_);
	finish(true);
}

void PaperProp::unload()
{
	QStringList up{instance_};
	if (!old_.isEmpty() && old_ != instance_)
		up << old_; // taken back while new writing went up: both go
	old_.clear();
	send(Step::Unload, "ItemUnloadRequest", unloadRequest(up), [this](bool ok, const QJsonObject &data) {
		say(ok ? QStringLiteral("the paper is down") : QStringLiteral("taking the paper down: %1").arg(message(data)));
		instance_.clear();
		pinnedTo_.clear();
		bends_ = false;
		finish(ok);
	});
}

void PaperProp::check()
{
	if (stopped_ || busy() || !shown())
		return;
	send(Step::Check, "ItemListRequest", listRequest(instance_), [this](bool ok, const QJsonObject &data) {
		bool there = true;
		if (ok) {
			there = false;
			for (const QJsonValue &item : data.value(QStringLiteral("itemInstancesInScene")).toArray())
				there = there || item.toObject().value(QStringLiteral("instanceID")).toString() == instance_;
		} else if (disconnected(data)) {
			there = false; // VTube Studio takes it down when the connection drops
		}
		if (!there) {
			say(QStringLiteral("the paper is no longer in VTube Studio"));
			instance_.clear();
			pinnedTo_.clear();
			bends_ = false;
			wanted_ = false;
		}
		finish(true);
	});
}

const QByteArray &PaperProp::picture()
{
	if (png_.isEmpty() && paintPicture_) {
		png_ = paintPicture_();
		paintPicture_ = nullptr;
	}
	return png_;
}

void PaperProp::say(const QString &line)
{
	if (said)
		said(line);
}

} // namespace sarxina::jobapplication
