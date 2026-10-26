// A Live2D model file (.moc3, version 3.00) written from scratch, for the paper as a Live2D item (paper-item.hpp).
//
// A .moc3 is the one binary file of a Live2D model, the one VTube Studio's Cubism runtime reads. Its layout was worked
// out by others from real models: the OpenL2D project (moc3.hexpat) and image2live2d
// (github.com/Wzhang3912/image2live2d, Apache-2.0, whose notes on what the native runtime checks are followed here:
// data from 0x7C0, every array on a 64-byte boundary, each keyform's positions on a boundary of 8 vertices, the file
// a multiple of 64 bytes, one byte of flags per mesh). This writes only what the paper needs: one part, no deformers,
// art meshes whose vertices are placed by parameters directly, one keyform for every combination of their keys.
//
// Positions are in the model's units (pixels / pixelsPerUnit) from the canvas origin, y down, as Cubism keeps them.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace sarxina::jobapplication::moc3 {

struct Parameter {
	QString id;
	float minimum = -1, maximum = 1, defaultValue = 0;
	std::vector<float> keys; // the values the meshes have keyforms at; none for a parameter only physics reads
};

struct Mesh {
	QString id;
	std::vector<float> uvs;               // u, v for each vertex, 0 to 1, v down
	std::vector<uint16_t> triangles;      // three vertex indices each
	std::vector<int> parameters;          // which parameters (with keys) place it; the first varies fastest
	std::vector<std::vector<float>> keyforms; // x, y for each vertex, one list per combination of keys
	std::vector<float> opacities;         // one per keyform; none: 1 everywhere
	int vertexCount() const { return static_cast<int>(uvs.size() / 2); }
};

struct Model {
	float canvasWidth = 1024, canvasHeight = 1024; // pixels
	float pixelsPerUnit = 1024;
	float originX = 512, originY = 512; // pixels, where 0, 0 is
	QString part = QStringLiteral("Part");
	std::vector<Parameter> parameters;
	std::vector<Mesh> meshes; // back to front
};

inline constexpr uint8_t kVersion300 = 1;
inline constexpr int kCountFields = 23;  // the count table of a 3.00 file
inline constexpr int kArrayFields = 99;  // the arrays after the count table and the canvas
inline constexpr int kDataStart = 0x7C0; // the runtime uses the bytes before this as scratch

// The file; empty if the model is not one this can write (says why in `why`).
QByteArray write(const Model &model, QString *why = nullptr);

// What a file says about itself, read back for the self-test.
struct Summary {
	bool ok = false;
	QString why;
	int version = 0;
	std::vector<uint32_t> counts;  // kCountFields
	std::vector<uint32_t> offsets; // count table, canvas, then each array
	float canvas[5] = {};          // pixelsPerUnit, originX, originY, width, height
	QStringList parameters, meshes;
	std::vector<float> keys;       // every parameter's keys, in order
	std::vector<int> keyformCounts; // per mesh
	std::vector<std::vector<float>> firstKeyform; // per mesh: its vertices at its first keyform
	std::vector<std::vector<float>> uvs;          // per mesh
};
Summary read(const QByteArray &file);

} // namespace sarxina::jobapplication::moc3
