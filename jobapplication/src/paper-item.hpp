// The paper as a Live2D item, so that it moves like paper: VTube Studio runs Live2D physics on Live2D items, and feeds
// them the item's own movement when it is pinned to the model ("dragging physics") as well as the face tracking the
// item's settings map to its parameters. The plugin writes the item, VTube Studio moves it.
//
// The item is one folder in VTube Studio's Items folder:
//   JobApplication.moc3          the sheet as meshes whose every vertex is placed by four parameters (moc3.hpp);
//   JobApplication.physics3.json pendulums that turn the face's angles and movement into those parameters, late,
//                                overshooting and settling;
//   JobApplication.vtube.json    VTube Studio's settings for it: which tracking drives which parameter, physics on;
//   JobApplication.model3.json, .cdi3.json, and the texture: the painted sheet (paper.hpp) with what is written on it.
//
// The sheet hangs from the middle of its top edge. Its four ways of moving (PaperPose):
//   swing  the whole sheet turning about that point, as a held page lags behind the hand and swings back;
//   curl   its lower part bending toward the camera or back toward the body;
//   twist  its bottom corners turning, one forward and one back;
//   ripple a wave down its lower half.
// Paper bends but does not stretch: every pose keeps the sheet's own lengths (place() bends it in 3D and looks at it
// from the front), and what is printed or written on it is the texture, so it stays as it is whatever the pose.
//
// What is written changes the texture, and so the folder's name (the texture's hash): VTube Studio loads a new item
// for new writing, never a remembered old one.
#pragma once

#include "form.hpp"
#include "moc3.hpp"

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <utility>
#include <vector>

namespace sarxina::jobapplication {

struct PaperPose {
	double swing = 0;  // degrees, clockwise on screen
	double curl = 0;   // -1 (bent back toward the body) to 1 (toward the camera)
	double twist = 0;  // -1 to 1: 1 brings the right-hand bottom corner forward
	double ripple = 0; // -1 to 1
};

// A point of the sheet: x across it from its middle, y down from its top edge, in the painted sheet's pixels. Where the
// pose puts it in the item: pixels from the point VTube Studio pins (a third of the way down the flat sheet), y down;
// and how far it came toward the camera.
QPointF place(double x, double y, const PaperPose &pose, double *depth = nullptr);

// The item's parameters. The first six are what its settings map the face tracking to; physics reads them. The last
// four bend the sheet; physics writes them.
inline constexpr const char *kTrackedParameters[] = {"ParamAngleX",    "ParamAngleY",    "ParamAngleZ",
						     "ParamBodyMoveX", "ParamBodyMoveY", "ParamBodyMoveZ"};
inline constexpr const char *kSwing = "ParamPaperSwing";
inline constexpr const char *kCurl = "ParamPaperCurl";
inline constexpr const char *kTwist = "ParamPaperTwist";
inline constexpr const char *kRipple = "ParamPaperRipple";
inline constexpr double kMostSwing = 45; // degrees

// The texture: 2048 pixels square, the sheet at 1:1 in its top-left, beside it the shadow and the two shadings.
inline constexpr int kTextureSide = 2048;
QRectF sheetInTexture();
QImage paperTexture(const QImage &sheet);

// The files of the item, given its texture as a PNG.
struct PaperItem {
	QString name; // the folder, which is also the name VTube Studio loads it by
	std::vector<std::pair<QString, QByteArray>> files;
};
inline constexpr const char *kItemPrefix = "SarxinaJobApplication-";
inline constexpr const char *kItemBase = "JobApplication";
PaperItem paperItem(const QByteArray &texturePng);

// The pieces of it, for the self-test and the preview.
moc3::Model paperModel();
QByteArray paperMoc(); // made once
QJsonObject paperPhysics();
QJsonObject paperVtubeSettings(const QString &name, const QString &modelId);

// Puts the item's folder in VTube Studio's Items folder, written whole before the model files that make VTube Studio
// see it. False, and why, if it cannot.
bool installItem(const QString &itemsFolder, const PaperItem &item, QString &why);
// Takes this plugin's other paper items out of the Items folder (never anything else); how many.
int removeOldItems(const QString &itemsFolder, const QString &keep);

// VTube Studio's Items folder: next to the VTube Studio that is running, or where Steam installed it; empty if neither.
// `itemsName` is the folder's name as VTube Studio gives it (VTSFolderInfoRequest), normally "Items".
QString findItemsFolder(const QString &itemsName);
QString itemsFolderBeside(const QString &vtubeStudioExe, const QString &itemsName);

} // namespace sarxina::jobapplication
