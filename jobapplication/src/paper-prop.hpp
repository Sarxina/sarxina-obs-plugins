// The paper as an item in VTube Studio, pinned to the model so that it moves with the body, in the same frame as the
// model, because VTube Studio draws both. It comes up as a Live2D item (paper-item.hpp), which VTube Studio bends with
// its physics as the body moves: late, past and back, flapping on a head whip. If that cannot be done, it comes up as
// a flat picture instead, which only leans, sways and breathes with the ArtMesh it is pinned to.
//
// Bringing it up:
//   1. CurrentModelRequest: which model is loaded, and where, so the paper loads beside it.
//   2. ArtMeshListRequest, once per model: the ArtMesh to pin to (pinTarget below).
//   3. The Live2D item:
//      a. VTSFolderInfoRequest, once: the name of VTube Studio's Items folder; the folder itself is found beside the
//         VTube Studio that is running (findItemsFolder).
//      b. The item's folder written there, named after what is written on the paper (installItem).
//      c. ItemListRequest of the item files: VTube Studio reads the folder again, and says it sees the item; asked
//         a few times, half a second apart.
//      d. ItemLoadRequest of the item by that name, in front of the model, fading in. No permission is needed for it.
//      If any of these fails, the picture (paper.hpp) instead: ItemLoadRequest with it as custom data. Loading a
//      picture needs the LoadCustomImagesAsItems permission, which VTube Studio asks the streamer for once
//      (PermissionRequest); until it is given the loads are tried again every two seconds, for a minute.
//      VTube Studio loads no item while one of its menus or popups is open (its start-up notices, the first-time tip
//      about dragging items): the load is tried again every few seconds for as long as the paper is wanted.
//   4. ItemPinRequest: the item's centre (the picture's, or the Live2D item's origin, at the same place on the sheet)
//      to the middle of the ArtMesh, at a slight tilt of its own.
// Once a Live2D item is up, the plugin's older ones (with other writing) are taken out of the Items folder.
// New writing while it is up (rewrite): the new paper is loaded and pinned the same way, just behind the one that is
// up, and taken down once the new one has settled: pinning makes VTube Studio's physics swing an item hard for a
// moment (the pin's jump from where it loads), and the old paper hides that. Then the old one is taken down, and the
// paper never goes away. Each new one goes one place further back, from 30 down to 1; after that one comes in front
// again (and swings where it can be seen). The old one is unpinned and moved off the picture at once before it is
// unloaded: VTube Studio shows an item shrinking away as it unloads, and in front of the new paper that would be seen.
// Taking it down: ItemUnloadRequest. While it is up, ItemListRequest every few seconds says whether it is still there
// (VTube Studio takes it down itself if its connection to the manager drops, or the streamer can remove it).
//
// A toggle while it is coming up or going down is not lost: the paper ends the way the last toggle wanted.
#pragma once

#include "paper-item.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <map>

namespace sarxina::jobapplication {

// The ArtMesh to pin the paper to, from the model's ArtMesh IDs, or empty if none looks like the chest or body. The
// IDs riggers give are read as words: the chest, torso or body count, the middle of one counts more, and a shade,
// glow, outline, left or right part counts against. VTube Studio's "Center" pin is the middle triangle of the
// ArtMesh's list, not its middle in space, so a small ArtMesh at the middle of the chest pins best.
QString pinTarget(const QStringList &artMeshes);

// The requests, as VTube Studio's API takes them.
inline constexpr const char *kPictureName = "sarxina-job-application.png";
inline constexpr int kOrder = 30;           // in front of the model, which is at 0; new writing goes one place behind
inline constexpr double kFadeSeconds = 0.3;
// VTube Studio draws the Live2D item about three times as large as the picture at the same size, so the item's size
// is a third of the picture's: the streamer's 90 % is 0.3, the sheet from the collar down, about as wide as the torso.
inline constexpr double kItemScale = 1.0 / 3.0;
QJsonObject loadRequest(const QByteArray &png, double size, double rotation, double x, double y, int order = kOrder);
QJsonObject itemLoadRequest(const QString &item, double size, double rotation, double x, double y, int order = kOrder);
QJsonObject itemFilesRequest(const QString &item);
QJsonObject folderRequest();
QJsonObject pinRequest(const QString &instance, const QString &model, const QString &artMesh, double size,
		       double tilt);
QJsonObject unloadRequest(const QStringList &instances);
QJsonObject unpinRequest(const QString &instance);
QJsonObject moveAwayRequest(const QString &instance); // off the picture, at once
QJsonObject listRequest(const QString &instance);
QJsonObject permissionRequest();

class PaperProp : public QObject {
public:
	// Called once with VTube Studio's answer, or ok false and {"message": ..., maybe "errorID": ...}.
	using Answer = std::function<void(bool ok, const QJsonObject &data)>;
	// Sends a request to VTube Studio; false if it cannot be sent at all, and then `answer` is never called.
	using Send = std::function<bool(const QString &type, const QJsonObject &data, Answer answer)>;

	explicit PaperProp(Send send);

	// The paper's picture, as a PNG, and its size as VTube Studio counts item sizes (0 to 1). Both apply the next time
	// it comes up.
	void setPicture(const QByteArray &png)
	{
		png_ = png;
		paintPicture_ = nullptr;
	}
	// Or how to paint it, done only if it is needed: when the Live2D item cannot be used.
	void setPicture(std::function<QByteArray()> paint)
	{
		png_.clear();
		paintPicture_ = std::move(paint);
	}
	bool hasPicture() const { return !png_.isEmpty() || paintPicture_; }
	// The Live2D item's texture (paperTexture), as a PNG: the same sheet, for the item that bends. Without it, or with
	// `live2d` false, the paper comes up as the picture.
	void setTexture(const QByteArray &png) { texture_ = png; }
	bool hasTexture() const { return !texture_.isEmpty(); }
	bool live2d = true;
	// Where VTube Studio's Items folder is, given its name; findItemsFolder unless the self-test says otherwise.
	std::function<QString(const QString &itemsName)> findItems;
	void setSize(double size) { size_ = size; }
	// Its tilt: random by default, a few degrees either way each time it comes up.
	std::function<double()> tilt;

	void toggle();
	void show();
	void hide();
	// New writing (setPicture and setTexture were given it): if the paper is up, the new paper goes up in its place.
	void rewrite();
	// Which writing was handed over last, as the plugin counts it, and what is called once the paper on screen has
	// that writing on it (or a later one).
	int writing = 0;
	std::function<void(int writing)> wrote;
	// Whether it is up, and whether it is meant to be.
	bool shown() const { return !instance_.isEmpty(); }
	bool wanted() const { return wanted_; }
	bool busy() const { return step_ != Step::Idle; }
	// While it is up and nothing is going on, asks VTube Studio whether it is still there.
	void check();
	// The plugin is going away: nothing more is sent.
	void stop();

	// What happened, a line at a time, for the log.
	std::function<void(const QString &line)> said;
	// What it is pinned to, once it was.
	QString pinnedTo() const { return pinnedTo_; }
	QString instance() const { return instance_; }
	// Whether what is up is the Live2D item, and which; and the Items folder it went in.
	bool bends() const { return bends_; }
	QString itemName() const { return itemName_; }
	QString itemsFolder() const { return itemsFolder_; }

	// How long to wait before trying a load again, in milliseconds; less in the self-test.
	int retryMs = 2000;
	int retries = 30;
	int menusRetryMs = 3000; // while VTube Studio has a menu or popup open: for as long as the paper is wanted
	int settleMs = 2400;     // how long new writing settles behind the paper that is up before that one goes
	int listRetryMs = 500;
	int listTries = 4;

private:
	enum class Step { Idle, Model, Meshes, Folder, Listing, Permission, Load, Pin, Unload, Check };

	void go(); // towards what is wanted
	void finish(bool ok);
	bool send(Step step, const char *type, const QJsonObject &data, std::function<void(bool, const QJsonObject &)> then);
	void askModel();
	void askMeshes();
	void load();
	void loadItem(); // the Live2D item: steps 3a to 3d
	void listItem();
	void itemLoaded(bool ok, const QJsonObject &data);
	void fallBack(const QString &why); // the picture instead
	void loadPicture();
	void loaded(bool ok, const QJsonObject &data);
	void up(const QJsonObject &data);
	void retryLoad(const QString &why);
	void waitForMenus();
	void swap();
	void swapped(); // the new paper is pinned: the old one goes, once the new one has settled
	void takeOldAway();
	void oldGone(bool ok, const QJsonObject &data);
	void pin();
	void unload();
	void say(const QString &line);

	Send send_;
	QByteArray png_;
	std::function<QByteArray()> paintPicture_;
	const QByteArray &picture(); // png_, painted first if it has not been
	QByteArray texture_;
	double size_ = 0.9;
	double itemSize() const { return size_ * kItemScale; }
	bool wanted_ = false;
	bool stale_ = false;   // new writing to put up
	QString old_;          // while new writing goes up: the paper it replaces, until the new one is pinned
	bool oldBends_ = false;
	int order_ = kOrder;
	bool behind_ = false;  // the new writing went behind the paper it replaces
	int loading_ = 0;      // the writing being brought up
	int showing_ = 0;      // the writing on the paper that is up
	bool waitingForMenus_ = false;
	bool stopped_ = false;
	Step step_ = Step::Idle;
	uint64_t generation_ = 0; // answers to anything sent before stop() are dropped

	QString model_;
	double modelX_ = 0, modelY_ = 0;
	std::map<QString, QString> pins_; // model ID -> ArtMesh, or empty for none
	QString instance_;
	QString pinnedTo_;
	double tilt_ = 0;
	bool askedPermission_ = false;
	int tries_ = 0;
	QString itemsName_;   // VTube Studio's name for its Items folder, once asked
	QString itemsFolder_; // where the item went
	QString itemName_;    // the item written there, or being brought up
	PaperItem item_;      // the item for texture_, made once for it
	QByteArray itemFor_;
	bool bends_ = false;  // the Live2D item is what is up
	bool itemFailed_ = false; // VTube Studio would not load it: the picture from now on
	int listed_ = 0;
};

} // namespace sarxina::jobapplication
