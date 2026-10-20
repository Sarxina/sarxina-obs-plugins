#include "paper-item.hpp"

#include "paper.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLinearGradient>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace sarxina::jobapplication {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double W = kSheetWidth, H = kSheetHeight;

// ---- how the sheet bends ---------------------------------------------------------------------------------------------

constexpr double kHeld = 0.10;      // the top tenth stays flat, as held
constexpr double kMostCurl = 72;    // degrees the bottom edge turns out of the page at curl 1
constexpr double kMostRipple = 26;  // degrees of the wave at ripple 1
constexpr double kMostTwist = 62;   // degrees the bottom edge turns about the sheet's middle at twist 1
constexpr double kTwistFrom = 0.28; // where down the sheet the twist starts
constexpr double kFocal = 2.6 * H;  // how far the camera is, for the near part to look larger

double down(double y) // 0 at the end of the held part, 1 at the bottom, more past it
{
	return std::max(0.0, (y / H - kHeld) / (1 - kHeld));
}

// The angle (radians) the sheet's surface makes with the page at y: bending toward the camera is positive.
double outOfPage(double y, const PaperPose &pose)
{
	const double q = down(y);
	const double curl = pose.curl * kMostCurl * std::pow(q, 1.6);
	const double ripple = pose.ripple * kMostRipple * std::sin(2 * kPi * std::min(q, 1.0)) * std::min(q, 1.0);
	return (curl + ripple) * kPi / 180;
}

// A pose's bend, worked out once: the sheet's middle line from the top edge down, as height and depth.
class Bend {
public:
	explicit Bend(const PaperPose &pose) : pose_(pose)
	{
		const int steps = static_cast<int>(std::ceil((H + 2 * kStep) / kStep));
		height_.resize(steps + 1);
		depth_.resize(steps + 1);
		double y = 0, z = 0;
		for (int i = 0; i <= steps; i++) {
			height_[i] = y;
			depth_[i] = z;
			const double angle = outOfPage((i + 0.5) * kStep, pose); // the middle of the step
			y += kStep * std::cos(angle);
			z += kStep * std::sin(angle);
		}
		const double swing = pose.swing * kPi / 180;
		cos_ = std::cos(swing);
		sin_ = std::sin(swing);
	}

	QPointF at(double x, double y, double *depthOut) const
	{
		// down the middle line, flat above the top edge
		double cy = y, cz = 0;
		if (y > 0) {
			const double f = y / kStep;
			const int i = std::min(static_cast<int>(f), static_cast<int>(height_.size()) - 2);
			const double t = f - i;
			cy = height_[i] + (height_[i + 1] - height_[i]) * t;
			cz = depth_[i] + (depth_[i + 1] - depth_[i]) * t;
			if (f > height_.size() - 1) { // past the table: straight on
				const double angle = outOfPage(y, pose_);
				const double past = y - (height_.size() - 1) * kStep;
				cy = height_.back() + past * std::cos(angle);
				cz = depth_.back() + past * std::sin(angle);
			}
		}
		// across: turned about the middle line by the twist
		const double twist = pose_.twist * kMostTwist * kPi / 180 *
				     std::pow(std::max(0.0, (y / H - kTwistFrom) / (1 - kTwistFrom)), 1.3);
		const double X = x * std::cos(twist), Y = cy, Z = cz + x * std::sin(twist);
		// the whole sheet turned about the middle of its top edge, then seen from the front
		const double sx = X * cos_ - Y * sin_, sy = X * sin_ + Y * cos_;
		const double closer = kFocal / (kFocal - Z);
		if (depthOut)
			*depthOut = Z;
		return {sx * closer, sy * closer - kPinDown * H};
	}

private:
	static constexpr double kStep = 2;
	PaperPose pose_;
	std::vector<double> height_, depth_;
	double cos_ = 1, sin_ = 0;
};

// The sheet held slightly out of square, as in paper.cpp's picture: its corners moved a few pixels.
QPointF outOfSquare(double x, double y)
{
	const double a = (x + W / 2) / W, b = y / H;
	const QPointF topLeft(0.012 * W, 3), topRight(-0.008 * W, 0), bottomRight(0, -2), bottomLeft(2, 0);
	const QPointF moved = (1 - a) * (1 - b) * topLeft + a * (1 - b) * topRight + a * b * bottomRight + (1 - a) * b * bottomLeft;
	return {x + moved.x(), y + moved.y()};
}

// ---- the texture ------------------------------------------------------------------------------------------------------

constexpr int kAt = 16;           // the sheet's corner in the texture
constexpr int kEdge = 4;          // the sheet's meshes reach this far past its edge, so the edge is the texture's
constexpr int kShadowPad = 40;    // the shadow reaches this far past the sheet
const QRect kShadowSprite(1160, 16, 296, 376);
const QRect kDarkSprite(1160, 420, 520, 520);
const QRect kLightSprite(1160, 960, 520, 520);
constexpr int kSpriteBorder = 2;

void paintShadow(QPainter &painter)
{
	// The sheet's outline a quarter the size, blurred by painting it smaller still and scaling it up.
	QImage small(kShadowSprite.size() / 4, QImage::Format_ARGB32_Premultiplied);
	small.fill(Qt::transparent);
	{
		QPainter p(&small);
		p.setRenderHint(QPainter::Antialiasing);
		p.scale(small.width() / (W + 2 * kShadowPad), small.height() / (H + 2 * kShadowPad));
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 125));
		p.drawRect(QRectF(kShadowPad, kShadowPad, W, H));
	}
	painter.save();
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.drawImage(QRectF(kShadowSprite), small.scaled(kShadowSprite.size(), Qt::IgnoreAspectRatio,
							     Qt::SmoothTransformation));
	painter.restore();
}

// A shading for the sheet: none at the top, more toward the bottom, in `color`, up to `most` (0 to 255).
void paintShading(QPainter &painter, const QRect &sprite, const QColor &color, int most)
{
	const QRectF inside = QRectF(sprite).adjusted(kSpriteBorder, kSpriteBorder, -kSpriteBorder, -kSpriteBorder);
	QLinearGradient gradient(inside.topLeft(), inside.bottomLeft());
	for (double t = 0; t <= 1.0001; t += 0.05) {
		const double q = std::clamp((t - 0.12) / 0.88, 0.0, 1.0);
		QColor c = color;
		c.setAlpha(static_cast<int>(std::lround(most * q * q * (3 - 2 * q))));
		gradient.setColorAt(std::min(t, 1.0), c);
	}
	painter.fillRect(inside, gradient);
}

// ---- the meshes -------------------------------------------------------------------------------------------------------

struct Grid {
	int columns, rows;
	QRectF sheet;  // the part of the sheet it covers (x from the middle, y from the top edge), sheet pixels
	QRectF sprite; // where that is in the texture
};

const Grid kSheetGrid{8, 17, QRectF(-W / 2 - kEdge, -kEdge, W + 2 * kEdge, H + 2 * kEdge),
		      QRectF(kAt - kEdge, kAt - kEdge, W + 2 * kEdge, H + 2 * kEdge)};
const Grid kShadowGrid{8, 9, QRectF(-W / 2 - kShadowPad, -kShadowPad, W + 2 * kShadowPad, H + 2 * kShadowPad),
		       QRectF(kShadowSprite)};

std::vector<float> keyValues(std::initializer_list<float> values)
{
	return std::vector<float>(values);
}

const std::vector<float> kSwingKeys = keyValues({-45, -30, -15, 0, 15, 30, 45});
const std::vector<float> kCurlKeys = keyValues({-1, -0.5f, 0, 0.5f, 1});
const std::vector<float> kTwistKeys = keyValues({-1, -0.5f, 0, 0.5f, 1});
const std::vector<float> kRippleKeys = keyValues({-1, 0, 1});

constexpr double kPixelsPerUnit = 1185; // pictureSize().width(): the canvas is the picture's size

enum class Layer { Shadow, Sheet, Dark, Light };

moc3::Mesh mesh(const QString &id, const Grid &grid, const QRectF &sprite, Layer layer)
{
	moc3::Mesh m;
	m.id = id;
	std::vector<QPointF> material;
	for (int j = 0; j < grid.rows; j++)
		for (int i = 0; i < grid.columns; i++) {
			const double u = static_cast<double>(i) / (grid.columns - 1), v = static_cast<double>(j) / (grid.rows - 1);
			material.push_back(outOfSquare(grid.sheet.left() + u * grid.sheet.width(), grid.sheet.top() + v * grid.sheet.height()));
			m.uvs.push_back(static_cast<float>((sprite.left() + u * sprite.width()) / kTextureSide));
			m.uvs.push_back(static_cast<float>((sprite.top() + v * sprite.height()) / kTextureSide));
		}
	for (int j = 0; j + 1 < grid.rows; j++)
		for (int i = 0; i + 1 < grid.columns; i++) {
			const auto a = static_cast<uint16_t>(j * grid.columns + i), b = static_cast<uint16_t>(a + 1),
				   c = static_cast<uint16_t>(a + grid.columns), d = static_cast<uint16_t>(c + 1);
			m.triangles.insert(m.triangles.end(), {a, b, c, b, d, c});
		}
	m.parameters = {6, 7, 8, 9}; // swing, curl, twist, ripple: swing varies fastest
	for (float ripple : kRippleKeys)
		for (float twist : kTwistKeys)
			for (float curl : kCurlKeys)
				for (float swing : kSwingKeys) {
					const PaperPose pose{swing, curl, twist, ripple};
					const Bend bend(pose);
					std::vector<float> xy;
					for (const QPointF &point : material) {
						double depth = 0;
						QPointF at = bend.at(point.x(), point.y(), &depth);
						if (layer == Layer::Shadow) // on the body behind it: further off the nearer the sheet is
							at += QPointF(10 + 0.10 * depth, 16 + 0.16 * depth);
						xy.push_back(static_cast<float>(at.x() / kPixelsPerUnit));
						xy.push_back(static_cast<float>(at.y() / kPixelsPerUnit));
					}
					m.keyforms.push_back(std::move(xy));
					// shading: bent back, twisted or rippled away is darker; bent toward the camera, lighter
					const double dark = std::max(0.0, -static_cast<double>(curl)) * 0.85 + std::abs(twist) * 0.4 +
							    std::max(0.0, -static_cast<double>(ripple)) * 0.35;
					const double light = std::max(0.0, static_cast<double>(curl)) * 0.75 +
							     std::max(0.0, static_cast<double>(ripple)) * 0.3;
					if (layer == Layer::Dark)
						m.opacities.push_back(static_cast<float>(std::min(1.0, dark)));
					else if (layer == Layer::Light)
						m.opacities.push_back(static_cast<float>(std::min(1.0, light)));
				}
	return m;
}

// ---- the item's settings ----------------------------------------------------------------------------------------------

QJsonObject vertex(double y, double mobility, double delay, double acceleration, double radius)
{
	return {{"Position", QJsonObject{{"X", 0}, {"Y", y}}},
		{"Mobility", mobility},
		{"Delay", delay},
		{"Acceleration", acceleration},
		{"Radius", radius}};
}

QJsonObject input(const char *parameter, double weight, const char *type, bool reflect = false)
{
	return {{"Source", QJsonObject{{"Target", "Parameter"}, {"Id", parameter}}},
		{"Weight", weight},
		{"Type", type},
		{"Reflect", reflect}};
}

QJsonObject output(const char *parameter, int vertexIndex, double scale, bool reflect = false)
{
	return {{"Destination", QJsonObject{{"Target", "Parameter"}, {"Id", parameter}}},
		{"VertexIndex", vertexIndex},
		{"Scale", scale},
		{"Weight", 100},
		{"Type", "Angle"},
		{"Reflect", reflect}};
}

// A pendulum: hanging links of `length`, how freely they move, how fast, how hard gravity pulls them back.
QJsonArray chain(int links, double length, double mobility, double delay, double acceleration)
{
	QJsonArray out{vertex(0, 1, 1, 1, 0)};
	for (int i = 1; i <= links; i++)
		out << vertex(i * length, mobility, delay, acceleration, length);
	return out;
}

QJsonObject setting(const char *id, QJsonArray inputs, QJsonArray outputs, QJsonArray vertices)
{
	const QJsonObject range{{"Minimum", -10}, {"Default", 0}, {"Maximum", 10}};
	return {{"Id", id},
		{"Input", inputs},
		{"Output", outputs},
		{"Vertices", vertices},
		{"Normalization", QJsonObject{{"Position", range}, {"Angle", range}}}};
}

QJsonObject mapping(const char *name, const char *tracking, double inLow, double inHigh, const char *parameter,
		    double outLow, double outHigh)
{
	return {{"Folder", ""},
		{"Name", name},
		{"Input", tracking},
		{"InputRangeLower", inLow},
		{"InputRangeUpper", inHigh},
		{"OutputRangeLower", outLow},
		{"OutputRangeUpper", outHigh},
		{"ClampInput", false},
		{"ClampOutput", false},
		{"UseBlinking", false},
		{"UseBreathing", false},
		{"OutputLive2D", parameter},
		{"Smoothing", 8},
		{"Minimized", false}};
}

QByteArray json(const QJsonObject &object)
{
	return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QByteArray pngOf(const QImage &image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}

} // namespace

QPointF place(double x, double y, const PaperPose &pose, double *depth)
{
	return Bend(pose).at(x, y, depth);
}

QRectF sheetInTexture()
{
	return {static_cast<double>(kAt), static_cast<double>(kAt), W, H};
}

QImage paperTexture(const QImage &sheet)
{
	QImage texture(kTextureSide, kTextureSide, QImage::Format_ARGB32_Premultiplied);
	texture.fill(Qt::transparent);
	QPainter painter(&texture);
	painter.drawImage(QPointF(kAt, kAt), sheet);
	paintShadow(painter);
	paintShading(painter, kDarkSprite, QColor(20, 18, 30), 150);
	paintShading(painter, kLightSprite, QColor(255, 253, 245), 120);
	painter.end();
	return texture.convertToFormat(QImage::Format_ARGB32);
}

moc3::Model paperModel()
{
	moc3::Model model;
	const QSize canvas = pictureSize();
	model.canvasWidth = static_cast<float>(canvas.width());
	model.canvasHeight = static_cast<float>(canvas.height());
	model.pixelsPerUnit = static_cast<float>(kPixelsPerUnit);
	model.originX = model.canvasWidth / 2; // the pin, a third of the way down the flat sheet
	model.originY = model.canvasHeight / 2;
	model.part = QStringLiteral("Paper");
	model.parameters = {
		{kTrackedParameters[0], -30, 30, 0, {}}, {kTrackedParameters[1], -30, 30, 0, {}},
		{kTrackedParameters[2], -30, 30, 0, {}}, {kTrackedParameters[3], -10, 10, 0, {}},
		{kTrackedParameters[4], -10, 10, 0, {}}, {kTrackedParameters[5], -10, 10, 0, {}},
		{kSwing, -45, 45, 0, kSwingKeys},        {kCurl, -1, 1, 0, kCurlKeys},
		{kTwist, -1, 1, 0, kTwistKeys},          {kRipple, -1, 1, 0, kRippleKeys},
	};
	const QRectF sheetUv = kSheetGrid.sprite;
	const auto inSprite = [](const QRect &sprite) { return QRectF(sprite); };
	model.meshes.push_back(mesh(QStringLiteral("PaperShadow"), kShadowGrid, kShadowGrid.sprite, Layer::Shadow));
	model.meshes.push_back(mesh(QStringLiteral("PaperSheet"), kSheetGrid, sheetUv, Layer::Sheet));
	model.meshes.push_back(mesh(QStringLiteral("PaperShadeDark"), kSheetGrid, inSprite(kDarkSprite), Layer::Dark));
	model.meshes.push_back(mesh(QStringLiteral("PaperShadeLight"), kSheetGrid, inSprite(kLightSprite), Layer::Light));
	return model;
}

QByteArray paperMoc()
{
	static const QByteArray moc = moc3::write(paperModel());
	return moc;
}

// Four pendulums. Sideways movement of the body and the head swing the sheet and twist its corners; up and down,
// nodding, leaning in and turning curl it; all of it sets off the quick ripple. They are what the face tracking
// (mapped in paperVtubeSettings) and VTube Studio's dragging physics move. Tuned on Sarxina's model in Live2D's own
// physics (the moving preview): a lean swings the sheet about 15 degrees, late, past upright and back, settling in
// about a second; a head whip flaps it all the way, corners and all.
QJsonObject paperPhysics()
{
	QJsonArray settings{
		// the head turning right carries the chest right, so the sheet's bottom is left behind: reflected
		setting("PhysicsSetting1",
			{input("ParamBodyMoveX", 70, "X", true), input("ParamAngleX", 75, "X", true),
			 input("ParamAngleZ", 55, "Angle")},
			{output(kSwing, 1, 70)}, chain(2, 14, 0.94, 0.85, 1.2)),
		setting("PhysicsSetting2",
			{input("ParamBodyMoveY", 70, "X"), input("ParamAngleY", 50, "X"), input("ParamBodyMoveZ", 40, "X"),
			 input("ParamAngleX", 35, "X")},
			{output(kCurl, 1, 2.2)}, chain(2, 10, 0.92, 0.9, 1.0)),
		setting("PhysicsSetting3",
			{input("ParamAngleX", 60, "X"), input("ParamBodyMoveX", 50, "X", true), input("ParamAngleZ", 40, "Angle")},
			{output(kTwist, 2, 2.0)}, chain(2, 6, 0.88, 0.75, 1.4)),
		setting("PhysicsSetting4",
			{input("ParamAngleY", 40, "X"), input("ParamAngleX", 40, "X"), input("ParamBodyMoveY", 40, "X")},
			{output(kRipple, 2, 2.0)}, chain(2, 5, 0.85, 0.75, 1.5)),
	};
	int inputs = 0, outputs = 0, vertices = 0;
	QJsonArray dictionary;
	const char *names[] = {"Swing", "Curl", "Twist", "Ripple"};
	for (int i = 0; i < settings.size(); i++) {
		const QJsonObject s = settings[i].toObject();
		inputs += s.value("Input").toArray().size();
		outputs += s.value("Output").toArray().size();
		vertices += s.value("Vertices").toArray().size();
		dictionary << QJsonObject{{"Id", s.value("Id")}, {"Name", names[i]}};
	}
	return {{"Version", 3},
		{"Meta",
		 QJsonObject{{"PhysicsSettingCount", settings.size()},
			     {"TotalInputCount", inputs},
			     {"TotalOutputCount", outputs},
			     {"VertexCount", vertices},
			     {"Fps", 60},
			     {"EffectiveForces", QJsonObject{{"Gravity", QJsonObject{{"X", 0}, {"Y", -1}}},
							     {"Wind", QJsonObject{{"X", 0}, {"Y", 0}}}}},
			     {"PhysicsDictionary", dictionary}}},
		{"PhysicsSettings", settings}};
}

QJsonObject paperVtubeSettings(const QString &name, const QString &modelId)
{
	const QString base = QString::fromLatin1(kItemBase);
	return {
		{"Version", 1},
		{"Name", name},
		{"ModelID", modelId},
		{"FileReferences", QJsonObject{{"Icon", "icon.png"},
					       {"Model", base + ".model3.json"},
					       {"IdleAnimation", ""},
					       {"IdleAnimationWhenTrackingLost", ""}}},
		{"ModelPositionMovement",
		 QJsonObject{{"Use", false}, {"X", 6}, {"Y", 8}, {"Z", 11}, {"SmoothingX", 10}, {"SmoothingY", 10},
			     {"SmoothingZ", 10}}},
		{"ItemSettings", QJsonObject{{"OnlyMoveWhenPinned", false},
					     {"AllowNormalHotkeyTriggers", false},
					     {"Multiplier_HeadAngleX", 1.0},
					     {"Multiplier_HeadAngleY", 1.0},
					     {"Multiplier_HeadAngleZ", 1.0},
					     {"Shift_HeadAngleX", 0.0},
					     {"Shift_HeadAngleY", 0.0},
					     {"Smoothing_HeadAngleX", 8.0},
					     {"Smoothing_HeadAngleY", 8.0},
					     {"Smoothing_HeadAngleZ", 8.0}}},
		{"PhysicsSettings", QJsonObject{{"Use", true},
						{"UseLegacyPhysics", false},
						{"Live2DPhysicsFPS", 3},
						{"PhysicsStrength", 50},
						{"WindStrength", 0},
						{"DraggingPhysicsStrength", 70}}},
		{"GeneralSettings", QJsonObject{{"ModelVolume", 0},
						{"TimeUntilTrackingLostIdleAnimation", 0.0},
						{"WorkshopSharingForbidden", true},
						{"EnableExpressionSaving", false}}},
		{"ParameterSettings",
		 QJsonArray{mapping("Paper: head left/right", "FaceAngleX", -30, 30, "ParamAngleX", -30, 30),
			    mapping("Paper: head up/down", "FaceAngleY", -20, 20, "ParamAngleY", -30, 30),
			    mapping("Paper: head tilt", "FaceAngleZ", -30, 30, "ParamAngleZ", -30, 30),
			    mapping("Paper: body left/right", "FacePositionX", -10, 10, "ParamBodyMoveX", -10, 10),
			    mapping("Paper: body up/down", "FacePositionY", -10, 10, "ParamBodyMoveY", -10, 10),
			    mapping("Paper: body in/out", "FacePositionZ", -10, 10, "ParamBodyMoveZ", -10, 10)}},
		{"Hotkeys", QJsonArray{}},
		{"HotkeySettings", QJsonObject{{"UseOnScreenHotkeys", false},
					       {"UseKeyboardHotkeys", false},
					       {"SendOnScreenHotkeysToPC", false},
					       {"OnScreenHotkeyAlpha", 75}}},
	};
}

PaperItem paperItem(const QByteArray &texturePng)
{
	const QString base = QString::fromLatin1(kItemBase);
	const QByteArray moc = paperMoc();
	const QByteArray physics = json(paperPhysics());

	// The name: what is written (the texture) and how it moves (the model and physics), as a hash.
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(texturePng);
	hash.addData(moc);
	hash.addData(physics);
	const QByteArray digest = hash.result().toHex();
	PaperItem item;
	item.name = QString::fromLatin1(kItemPrefix) + QString::fromLatin1(digest.left(10));
	const QString modelId = QString::fromLatin1(digest.mid(10, 32));

	const QString textureFile = base + QStringLiteral(".2048/texture_00.png");
	const QJsonObject model3{
		{"Version", 3},
		{"FileReferences", QJsonObject{{"Moc", base + ".moc3"},
					       {"Textures", QJsonArray{textureFile}},
					       {"Physics", base + ".physics3.json"},
					       {"DisplayInfo", base + ".cdi3.json"}}},
		{"Groups", QJsonArray{QJsonObject{{"Target", "Parameter"}, {"Name", "LipSync"}, {"Ids", QJsonArray{}}},
				      QJsonObject{{"Target", "Parameter"}, {"Name", "EyeBlink"}, {"Ids", QJsonArray{}}}}},
	};
	QJsonArray parameters;
	const char *named[][2] = {{"ParamAngleX", "Head left/right"},   {"ParamAngleY", "Head up/down"},
				  {"ParamAngleZ", "Head tilt"},          {"ParamBodyMoveX", "Body left/right"},
				  {"ParamBodyMoveY", "Body up/down"},    {"ParamBodyMoveZ", "Body in/out"},
				  {kSwing, "Paper swing"},               {kCurl, "Paper curl"},
				  {kTwist, "Paper twist"},               {kRipple, "Paper ripple"}};
	for (const auto &pair : named)
		parameters << QJsonObject{{"Id", pair[0]}, {"GroupId", ""}, {"Name", pair[1]}};
	const QJsonObject cdi3{{"Version", 3},
			       {"Parameters", parameters},
			       {"ParameterGroups", QJsonArray{}},
			       {"Parts", QJsonArray{QJsonObject{{"Id", "Paper"}, {"Name", "Paper"}}}}};

	// The icon VTube Studio shows in its item list: the sheet, small.
	const QImage texture = QImage::fromData(texturePng);
	const QImage icon = texture.copy(sheetInTexture().toRect())
				    .scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	item.files = {
		{textureFile, texturePng},
		{base + ".moc3", moc},
		{base + ".physics3.json", physics},
		{base + ".cdi3.json", json(cdi3)},
		{QStringLiteral("icon.png"), pngOf(icon)},
		// these two last: they make it an item
		{base + ".model3.json", json(model3)},
		{base + ".vtube.json", json(paperVtubeSettings(QStringLiteral("Sarxina Job Application"), modelId))},
	};
	return item;
}

bool installItem(const QString &itemsFolder, const PaperItem &item, QString &why)
{
	const QDir items(itemsFolder);
	if (itemsFolder.isEmpty() || !items.exists()) {
		why = QStringLiteral("VTube Studio's Items folder was not found");
		return false;
	}
	const QDir folder(items.filePath(item.name));
	for (const auto &[path, bytes] : item.files) {
		const QString file = folder.filePath(path);
		if (!QDir().mkpath(QFileInfo(file).absolutePath())) {
			why = QStringLiteral("cannot make a folder in %1").arg(QDir::toNativeSeparators(itemsFolder));
			return false;
		}
		QSaveFile out(file);
		if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
			why = QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(file));
			return false;
		}
	}
	return true;
}

int removeOldItems(const QString &itemsFolder, const QString &keep)
{
	if (itemsFolder.isEmpty())
		return 0;
	static const QRegularExpression ours(QStringLiteral("^%1[0-9a-f]{10}$").arg(QLatin1String(kItemPrefix)));
	const QDir items(itemsFolder);
	int removed = 0;
	for (const QString &name : items.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (name == keep || !ours.match(name).hasMatch())
			continue;
		QDir folder(items.filePath(name));
		if (!folder.exists(QString::fromLatin1(kItemBase) + QStringLiteral(".vtube.json")))
			continue; // not one this plugin wrote
		removed += folder.removeRecursively();
	}
	return removed;
}

QString itemsFolderBeside(const QString &vtubeStudioExe, const QString &itemsName)
{
	const QFileInfo exe(vtubeStudioExe);
	const QString name = itemsName.isEmpty() ? QStringLiteral("Items") : itemsName;
	const QDir data(exe.absoluteDir().filePath(exe.completeBaseName() + QStringLiteral("_Data/StreamingAssets")));
	return data.exists(name) ? QDir::cleanPath(data.filePath(name)) : QString();
}

QString findItemsFolder(const QString &itemsName)
{
	QStringList installs;
#ifdef _WIN32
	// The VTube Studio that is running, wherever it is.
	if (HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); snapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
			if (_wcsicmp(entry.szExeFile, L"VTube Studio.exe") != 0)
				continue;
			if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)) {
				wchar_t path[MAX_PATH * 2];
				DWORD size = MAX_PATH * 2;
				if (QueryFullProcessImageNameW(process, 0, path, &size))
					installs << QString::fromWCharArray(path, static_cast<int>(size));
				CloseHandle(process);
			}
		}
		CloseHandle(snapshot);
	}
	// Where Steam installed it (app 1325860), in any of Steam's libraries.
	const QSettings uninstall(
		QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 1325860"),
		QSettings::NativeFormat);
	const QString installed = uninstall.value(QStringLiteral("InstallLocation")).toString();
	if (!installed.isEmpty())
		installs << QDir(installed).filePath(QStringLiteral("VTube Studio.exe"));
	const QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
	const QString steamPath = steam.value(QStringLiteral("SteamPath")).toString();
	if (!steamPath.isEmpty()) {
		QStringList libraries{steamPath};
		QFile folders(QDir(steamPath).filePath(QStringLiteral("steamapps/libraryfolders.vdf")));
		if (folders.open(QIODevice::ReadOnly)) {
			static const QRegularExpression path(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
			for (auto match = path.globalMatch(QString::fromUtf8(folders.readAll())); match.hasNext();)
				libraries << match.next().captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
		}
		for (const QString &library : libraries)
			installs << QDir(library).filePath(QStringLiteral("steamapps/common/VTube Studio/VTube Studio.exe"));
	}
#endif
	for (const QString &exe : installs)
		if (const QString items = itemsFolderBeside(exe, itemsName); !items.isEmpty())
			return items;
	return {};
}

} // namespace sarxina::jobapplication
