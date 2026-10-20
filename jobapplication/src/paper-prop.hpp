// The paper as an item in VTube Studio, pinned to the model so that it moves with the body: it leans, sways and
// breathes with the ArtMesh it is pinned to, in the same frame as the model, because VTube Studio draws both.
//
// Bringing it up:
//   1. CurrentModelRequest: which model is loaded, and where, so the paper loads beside it.
//   2. ArtMeshListRequest, once per model: the ArtMesh to pin to (pinTarget below).
//   3. ItemLoadRequest with the picture (paper.hpp) as custom data, in front of the model, fading in. Loading a
//      picture needs the LoadCustomImagesAsItems permission, which VTube Studio asks the streamer for once
//      (PermissionRequest); until it is given the loads are tried again every two seconds, for a minute.
//   4. ItemPinRequest: the picture's centre to the middle of the ArtMesh, at a slight tilt of its own.
// Taking it down: ItemUnloadRequest. While it is up, ItemListRequest every few seconds says whether it is still there
// (VTube Studio takes it down itself if its connection to the manager drops, or the streamer can remove it).
//
// A toggle while it is coming up or going down is not lost: the paper ends the way the last toggle wanted.
#pragma once

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
inline constexpr int kOrder = 20;           // in front of the model, which is at 0
inline constexpr double kFadeSeconds = 0.3;
QJsonObject loadRequest(const QByteArray &png, double size, double rotation, double x, double y);
QJsonObject pinRequest(const QString &instance, const QString &model, const QString &artMesh, double size,
		       double tilt);
QJsonObject unloadRequest(const QString &instance);
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
	void setPicture(const QByteArray &png) { png_ = png; }
	bool hasPicture() const { return !png_.isEmpty(); }
	void setSize(double size) { size_ = size; }
	// Its tilt: random by default, a few degrees either way each time it comes up.
	std::function<double()> tilt;

	void toggle();
	void show();
	void hide();
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

	// How long to wait before trying a load again, in milliseconds; less in the self-test.
	int retryMs = 2000;
	int retries = 30;

private:
	enum class Step { Idle, Model, Meshes, Permission, Load, Pin, Unload, Check };

	void go(); // towards what is wanted
	void finish(bool ok);
	bool send(Step step, const char *type, const QJsonObject &data, std::function<void(bool, const QJsonObject &)> then);
	void askModel();
	void askMeshes();
	void load();
	void loaded(bool ok, const QJsonObject &data);
	void retryLoad(const QString &why);
	void pin();
	void unload();
	void say(const QString &line);

	Send send_;
	QByteArray png_;
	double size_ = 0.9;
	bool wanted_ = false;
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
};

} // namespace sarxina::jobapplication
