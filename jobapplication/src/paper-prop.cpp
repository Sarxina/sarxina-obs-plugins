#include "paper-prop.hpp"

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

QJsonObject loadRequest(const QByteArray &png, double size, double rotation, double x, double y)
{
	return {
		{QStringLiteral("fileName"), QString::fromLatin1(kPictureName)},
		{QStringLiteral("positionX"), x},
		{QStringLiteral("positionY"), y},
		{QStringLiteral("size"), std::clamp(size, 0.0, 1.0)},
		{QStringLiteral("rotation"), rotation},
		{QStringLiteral("fadeTime"), kFadeSeconds},
		{QStringLiteral("order"), kOrder},
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

QJsonObject unloadRequest(const QString &instance)
{
	return {
		{QStringLiteral("unloadAllInScene"), false},
		{QStringLiteral("unloadAllLoadedByThisPlugin"), false}, // "this plugin" is the manager, for every plugin
		{QStringLiteral("allowUnloadingItemsLoadedByUserOrOtherPlugins"), false},
		{QStringLiteral("instanceIDs"), QJsonArray{instance}},
		{QStringLiteral("fileNames"), QJsonArray{}},
	};
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

PaperProp::PaperProp(Send send) : tilt(randomTilt), send_(std::move(send)) {}

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

void PaperProp::go()
{
	if (stopped_ || busy())
		return;
	if (wanted_ && !shown())
		askModel();
	else if (!wanted_ && shown())
		unload();
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
	if (png_.isEmpty()) {
		say(QStringLiteral("no picture of the paper"));
		wanted_ = false;
		finish(false);
		return;
	}
	if (!wanted_) { // taken back while waiting to try again
		tries_ = 0;
		finish(true);
		return;
	}
	tilt_ = tilt ? tilt() : 0;
	send(Step::Load, "ItemLoadRequest", loadRequest(png_, size_, tilt_, modelX_, modelY_),
	     [this](bool ok, const QJsonObject &data) { loaded(ok, data); });
}

void PaperProp::loaded(bool ok, const QJsonObject &data)
{
	if (ok) {
		tries_ = 0;
		instance_ = data.value(QStringLiteral("instanceID")).toString();
		if (instance_.isEmpty()) {
			say(QStringLiteral("VTube Studio loaded the paper but gave no instance ID"));
			wanted_ = false;
			finish(false);
		} else if (!wanted_) {
			unload(); // taken back while it loaded
		} else {
			pin();
		}
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
		retryLoad(QStringLiteral("VTube Studio cannot load items while its menus are open"));
		return;
	}
	say(QStringLiteral("VTube Studio did not load the paper: %1").arg(message(data)));
	wanted_ = false;
	finish(false);
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

void PaperProp::pin()
{
	const QString artMesh = pins_[model_];
	if (artMesh.isEmpty()) {
		say(QStringLiteral("the paper is up, not pinned"));
		finish(true);
		return;
	}
	send(Step::Pin, "ItemPinRequest", pinRequest(instance_, model_, artMesh, size_, tilt_),
	     [this, artMesh](bool ok, const QJsonObject &data) {
		     if (ok) {
			     pinnedTo_ = artMesh;
			     say(QStringLiteral("the paper is up, pinned to %1, tilted %2 degrees")
					 .arg(artMesh)
					 .arg(tilt_, 0, 'f', 1));
		     } else {
			     say(QStringLiteral("the paper is up but VTube Studio did not pin it: %1").arg(message(data)));
		     }
		     finish(true);
	     });
}

void PaperProp::unload()
{
	send(Step::Unload, "ItemUnloadRequest", unloadRequest(instance_), [this](bool ok, const QJsonObject &data) {
		say(ok ? QStringLiteral("the paper is down") : QStringLiteral("taking the paper down: %1").arg(message(data)));
		instance_.clear();
		pinnedTo_.clear();
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
			wanted_ = false;
		}
		finish(true);
	});
}

void PaperProp::say(const QString &line)
{
	if (said)
		said(line);
}

} // namespace sarxina::jobapplication
