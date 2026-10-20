#include "moc3.hpp"

#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace sarxina::jobapplication::moc3 {

namespace {

// What the counts count, in the order of the count table.
enum Count {
	Parts, Deformers, WarpDeformers, RotationDeformers, ArtMeshes, Parameters, PartKeyforms, WarpDeformerKeyforms,
	RotationDeformerKeyforms, ArtMeshKeyforms, KeyformPositions, ParameterBindingIndices, KeyformBindings,
	ParameterBindings, Keys, Uvs, PositionIndices, DrawableMasks, DrawOrderGroups, DrawOrderGroupObjects, Glue, GlueInfo,
	GlueKeyforms,
};

// How an array's elements are stored: runtime scratch (8 zero bytes), a 64-byte ID, 4-byte numbers, 2 or 1 bytes.
enum class Kind { Scratch, Id, S32, U32, F32, S16, U8 };

struct Field {
	Kind kind;
	Count count;
};

// Every array of a 3.00 file, in the order of the offset table. The keyform positions and the UVs count floats.
// (Their names, below, are in kFields' order.)
constexpr Field kFields[kArrayFields] = {
	// parts: scratch, ids, keyform binding, keyforms begin, keyforms count, visible, enabled, parent part
	{Kind::Scratch, Parts}, {Kind::Id, Parts}, {Kind::S32, Parts}, {Kind::S32, Parts}, {Kind::S32, Parts},
	{Kind::U32, Parts}, {Kind::U32, Parts}, {Kind::S32, Parts},
	// deformers: scratch, ids, keyform binding, visible, enabled, parent part, parent deformer, type, index in its type
	{Kind::Scratch, Deformers}, {Kind::Id, Deformers}, {Kind::S32, Deformers}, {Kind::U32, Deformers},
	{Kind::U32, Deformers}, {Kind::S32, Deformers}, {Kind::S32, Deformers}, {Kind::U32, Deformers},
	{Kind::S32, Deformers},
	// warp deformers: keyform binding, keyforms begin, keyforms count, vertex count, rows, columns
	{Kind::S32, WarpDeformers}, {Kind::S32, WarpDeformers}, {Kind::S32, WarpDeformers}, {Kind::S32, WarpDeformers},
	{Kind::U32, WarpDeformers}, {Kind::U32, WarpDeformers},
	// rotation deformers: keyform binding, keyforms begin, keyforms count, base angle
	{Kind::S32, RotationDeformers}, {Kind::S32, RotationDeformers}, {Kind::S32, RotationDeformers},
	{Kind::F32, RotationDeformers},
	// art meshes: four scratch, ids, keyform binding, keyforms begin, keyforms count, visible, enabled, parent part,
	// parent deformer, texture, flags (one byte), vertex count, UVs begin, indices begin, indices count, masks begin,
	// masks count
	{Kind::Scratch, ArtMeshes}, {Kind::Scratch, ArtMeshes}, {Kind::Scratch, ArtMeshes}, {Kind::Scratch, ArtMeshes},
	{Kind::Id, ArtMeshes}, {Kind::S32, ArtMeshes}, {Kind::S32, ArtMeshes}, {Kind::S32, ArtMeshes},
	{Kind::U32, ArtMeshes}, {Kind::U32, ArtMeshes}, {Kind::S32, ArtMeshes}, {Kind::S32, ArtMeshes},
	{Kind::U32, ArtMeshes}, {Kind::U8, ArtMeshes}, {Kind::S32, ArtMeshes}, {Kind::S32, ArtMeshes},
	{Kind::S32, ArtMeshes}, {Kind::S32, ArtMeshes}, {Kind::S32, ArtMeshes}, {Kind::S32, ArtMeshes},
	// parameters: scratch, ids, maximum, minimum, default, repeats, decimal places, bindings begin, bindings count
	{Kind::Scratch, Parameters}, {Kind::Id, Parameters}, {Kind::F32, Parameters}, {Kind::F32, Parameters},
	{Kind::F32, Parameters}, {Kind::U32, Parameters}, {Kind::U32, Parameters}, {Kind::S32, Parameters},
	{Kind::S32, Parameters},
	// part keyforms: draw order
	{Kind::F32, PartKeyforms},
	// warp deformer keyforms: opacity, positions begin
	{Kind::F32, WarpDeformerKeyforms}, {Kind::S32, WarpDeformerKeyforms},
	// rotation deformer keyforms: opacity, angle, origin x, origin y, scale, reflect x, reflect y
	{Kind::F32, RotationDeformerKeyforms}, {Kind::F32, RotationDeformerKeyforms}, {Kind::F32, RotationDeformerKeyforms},
	{Kind::F32, RotationDeformerKeyforms}, {Kind::F32, RotationDeformerKeyforms}, {Kind::U32, RotationDeformerKeyforms},
	{Kind::U32, RotationDeformerKeyforms},
	// art mesh keyforms: opacity, draw order, positions begin
	{Kind::F32, ArtMeshKeyforms}, {Kind::F32, ArtMeshKeyforms}, {Kind::S32, ArtMeshKeyforms},
	{Kind::F32, KeyformPositions},
	{Kind::S32, ParameterBindingIndices},
	{Kind::S32, KeyformBindings}, {Kind::S32, KeyformBindings},
	{Kind::S32, ParameterBindings}, {Kind::S32, ParameterBindings},
	{Kind::F32, Keys},
	{Kind::F32, Uvs},
	{Kind::S16, PositionIndices},
	{Kind::S32, DrawableMasks},
	// draw order groups: objects begin, objects count, objects in all, highest draw order, lowest draw order
	{Kind::S32, DrawOrderGroups}, {Kind::S32, DrawOrderGroups}, {Kind::S32, DrawOrderGroups},
	{Kind::U32, DrawOrderGroups}, {Kind::U32, DrawOrderGroups},
	// draw order group objects: type (0: an art mesh), index, group of its own (-1: none)
	{Kind::U32, DrawOrderGroupObjects}, {Kind::S32, DrawOrderGroupObjects}, {Kind::S32, DrawOrderGroupObjects},
	// glue
	{Kind::Scratch, Glue}, {Kind::Id, Glue}, {Kind::S32, Glue}, {Kind::S32, Glue}, {Kind::S32, Glue}, {Kind::S32, Glue},
	{Kind::S32, Glue}, {Kind::S32, Glue}, {Kind::S32, Glue},
	{Kind::F32, GlueInfo}, {Kind::S16, GlueInfo},
	{Kind::F32, GlueKeyforms},
};

// Every array's place in kFields, by name.
enum Slot {
	PartScratch, PartIds, PartBinding, PartKeyformsBegin, PartKeyformsCount, PartVisible, PartEnabled, PartParent,
	DeformerScratch, DeformerIds, DeformerBinding, DeformerVisible, DeformerEnabled, DeformerParentPart,
	DeformerParentDeformer, DeformerType, DeformerSpecific,
	WarpBinding, WarpKeyformsBegin, WarpKeyformsCount, WarpVertexCount, WarpRows, WarpColumns,
	RotationBinding, RotationKeyformsBegin, RotationKeyformsCount, RotationBaseAngle,
	MeshScratch0, MeshScratch1, MeshScratch2, MeshScratch3, MeshIds, MeshBinding, MeshKeyformsBegin,
	MeshKeyformsCount, MeshVisible, MeshEnabled, MeshParentPart, MeshParentDeformer, MeshTexture, MeshFlags,
	MeshVertexCount, MeshUvsBegin, MeshIndicesBegin, MeshIndicesCount, MeshMasksBegin, MeshMasksCount,
	ParameterScratch, ParameterIds, ParameterMaximum, ParameterMinimum, ParameterDefault, ParameterRepeats,
	ParameterDecimals, ParameterBindingsBegin, ParameterBindingsCount,
	PartKeyformDrawOrder,
	WarpKeyformOpacity, WarpKeyformPositionsBegin,
	RotationKeyformOpacity, RotationKeyformAngle, RotationKeyformOriginX, RotationKeyformOriginY,
	RotationKeyformScale, RotationKeyformReflectX, RotationKeyformReflectY,
	MeshKeyformOpacity, MeshKeyformDrawOrder, MeshKeyformPositionsBegin,
	PositionsXy,
	BindingIndices,
	KeyformBindingBegin, KeyformBindingCount,
	ParameterBindingKeysBegin, ParameterBindingKeysCount,
	KeyValues,
	UvValues,
	IndexValues,
	MaskValues,
	GroupObjectsBegin, GroupObjectsCount, GroupObjectsTotal, GroupHighest, GroupLowest,
	GroupObjectType, GroupObjectIndex, GroupObjectSelf,
	GlueScratch, GlueIds, GlueBinding, GlueKeyformsBegin, GlueKeyformsCount, GlueMeshA, GlueMeshB, GlueInfoBegin,
	GlueInfoCount,
	GlueInfoWeights, GlueInfoIndices,
	GlueKeyformIntensities,
	SlotCount,
};
static_assert(SlotCount == kArrayFields, "a name for every array");
static_assert(MeshFlags == 40 && ParameterIds == 48 && PositionsXy == 69 && GroupObjectSelf == 86, "in table order");

constexpr int kAlign = 64;
constexpr int kKeyformAlign = 8; // vertices: each keyform's positions start on a multiple of this
constexpr float kDrawOrder = 500;

int elementSize(Kind kind)
{
	switch (kind) {
	case Kind::Scratch:
		return 8;
	case Kind::Id:
		return 64;
	case Kind::S16:
		return 2;
	case Kind::U8:
		return 1;
	default:
		return 4;
	}
}

// The arrays as bytes, little-endian, before they are laid out.
struct Arrays {
	std::vector<QByteArray> bytes = std::vector<QByteArray>(kArrayFields);
	uint32_t counts[kCountFields] = {};

	void i32(int slot, int32_t value)
	{
		char raw[4];
		qToLittleEndian(value, raw);
		bytes[slot].append(raw, 4);
	}
	void u32(int slot, uint32_t value)
	{
		char raw[4];
		qToLittleEndian(value, raw);
		bytes[slot].append(raw, 4);
	}
	void f32(int slot, float value)
	{
		uint32_t bits;
		std::memcpy(&bits, &value, 4);
		u32(slot, bits);
	}
	void s16(int slot, int16_t value)
	{
		char raw[2];
		qToLittleEndian(value, raw);
		bytes[slot].append(raw, 2);
	}
	void u8(int slot, uint8_t value) { bytes[slot].append(static_cast<char>(value)); }
	void id(int slot, const QString &text)
	{
		QByteArray name = text.toLatin1().left(63);
		name.append(QByteArray(64 - name.size(), '\0'));
		bytes[slot].append(name);
	}
	void scratch(int slot, int count) { bytes[slot].append(QByteArray(8 * count, '\0')); }
};

void pad(QByteArray &file, int to)
{
	if (const qsizetype rest = file.size() % to)
		file.append(QByteArray(to - rest, '\0'));
}

uint32_t u32At(const QByteArray &file, qsizetype at)
{
	return qFromLittleEndian<uint32_t>(file.constData() + at);
}

float f32At(const QByteArray &file, qsizetype at)
{
	const uint32_t bits = u32At(file, at);
	float value;
	std::memcpy(&value, &bits, 4);
	return value;
}

} // namespace

QByteArray write(const Model &model, QString *why)
{
	const auto fail = [why](const QString &reason) {
		if (why)
			*why = reason;
		return QByteArray();
	};
	if (model.meshes.empty())
		return fail(QStringLiteral("no meshes"));

	Arrays a;
	const int meshCount = static_cast<int>(model.meshes.size());
	const int parameterCount = static_cast<int>(model.parameters.size());

	// Keys and parameter bindings: one binding for each parameter with keys.
	std::vector<int> bindingOf(model.parameters.size(), -1);
	int bindings = 0, keys = 0;
	for (int p = 0; p < parameterCount; p++) {
		const Parameter &parameter = model.parameters[p];
		if (parameter.id.isEmpty() || parameter.id.size() > 63)
			return fail(QStringLiteral("a parameter's ID is empty or too long"));
		a.scratch(ParameterScratch, 1);
		a.id(ParameterIds, parameter.id);
		a.f32(ParameterMaximum, parameter.maximum);
		a.f32(ParameterMinimum, parameter.minimum);
		a.f32(ParameterDefault, parameter.defaultValue);
		a.u32(ParameterRepeats, 0);
		a.u32(ParameterDecimals, 3);
		a.i32(ParameterBindingsBegin, bindings);
		a.i32(ParameterBindingsCount, parameter.keys.empty() ? 0 : 1);
		if (parameter.keys.empty())
			continue;
		if (!std::is_sorted(parameter.keys.begin(), parameter.keys.end()))
			return fail(QStringLiteral("the keys of %1 are not in order").arg(parameter.id));
		bindingOf[p] = bindings++;
		a.i32(ParameterBindingKeysBegin, keys);
		a.i32(ParameterBindingKeysCount, static_cast<int>(parameter.keys.size()));
		for (float key : parameter.keys)
			a.f32(KeyValues, key);
		keys += static_cast<int>(parameter.keys.size());
	}

	// Keyform bindings: the first is empty, for the part (and any mesh no parameter moves).
	int bindingIndices = 0;
	a.i32(KeyformBindingBegin, 0);
	a.i32(KeyformBindingCount, 0);
	int keyformBindings = 1;

	// The part.
	a.scratch(PartScratch, 1);
	a.id(PartIds, model.part);
	a.i32(PartBinding, 0);
	a.i32(PartKeyformsBegin, 0);
	a.i32(PartKeyformsCount, 1);
	a.u32(PartVisible, 1);
	a.u32(PartEnabled, 1);
	a.i32(PartParent, -1);
	a.f32(PartKeyformDrawOrder, kDrawOrder);

	// The meshes, their keyforms and their geometry.
	int keyforms = 0, positions = 0, uvs = 0, indices = 0;
	for (int m = 0; m < meshCount; m++) {
		const Mesh &mesh = model.meshes[m];
		const int vertices = mesh.vertexCount();
		if (vertices < 3 || mesh.uvs.size() % 2 || mesh.triangles.empty() || mesh.triangles.size() % 3)
			return fail(QStringLiteral("%1 has no proper geometry").arg(mesh.id));
		if (mesh.id.isEmpty() || mesh.id.size() > 63)
			return fail(QStringLiteral("a mesh's ID is empty or too long"));
		size_t expected = 1;
		for (int p : mesh.parameters) {
			if (p < 0 || p >= parameterCount || bindingOf[p] < 0)
				return fail(QStringLiteral("%1 is placed by a parameter without keys").arg(mesh.id));
			expected *= model.parameters[p].keys.size();
		}
		if (mesh.keyforms.size() != expected)
			return fail(QStringLiteral("%1 has %2 keyforms, not %3").arg(mesh.id).arg(mesh.keyforms.size()).arg(expected));
		if (!mesh.opacities.empty() && mesh.opacities.size() != expected)
			return fail(QStringLiteral("%1 has an opacity for some keyforms only").arg(mesh.id));
		for (uint16_t index : mesh.triangles)
			if (index >= vertices)
				return fail(QStringLiteral("%1 has a triangle past its vertices").arg(mesh.id));

		int binding = 0;
		if (!mesh.parameters.empty()) {
			binding = keyformBindings++;
			a.i32(KeyformBindingBegin, bindingIndices);
			a.i32(KeyformBindingCount, static_cast<int>(mesh.parameters.size()));
			for (int p : mesh.parameters)
				a.i32(BindingIndices, bindingOf[p]);
			bindingIndices += static_cast<int>(mesh.parameters.size());
		}

		a.id(MeshIds, mesh.id);
		a.i32(MeshBinding, binding);
		a.i32(MeshKeyformsBegin, keyforms);
		a.i32(MeshKeyformsCount, static_cast<int>(expected));
		a.u32(MeshVisible, 1);
		a.u32(MeshEnabled, 1);
		a.i32(MeshParentPart, 0);
		a.i32(MeshParentDeformer, -1);
		a.u32(MeshTexture, 0);
		a.u8(MeshFlags, 1 << 2); // double-sided: drawn whichever way its triangles wind
		a.i32(MeshVertexCount, vertices);
		a.i32(MeshUvsBegin, uvs);
		a.i32(MeshIndicesBegin, indices);
		a.i32(MeshIndicesCount, static_cast<int>(mesh.triangles.size()));
		a.i32(MeshMasksBegin, 0);
		a.i32(MeshMasksCount, 0);

		for (float uv : mesh.uvs)
			a.f32(UvValues, uv);
		uvs += static_cast<int>(mesh.uvs.size());
		for (uint16_t index : mesh.triangles)
			a.s16(IndexValues, static_cast<int16_t>(index));
		indices += static_cast<int>(mesh.triangles.size());

		// Each keyform's positions start on a multiple of 8 vertices, as the native runtime steps through them.
		const int stride = (vertices + kKeyformAlign - 1) / kKeyformAlign * kKeyformAlign;
		for (size_t k = 0; k < expected; k++) {
			const std::vector<float> &xy = mesh.keyforms[k];
			if (xy.size() != mesh.uvs.size())
				return fail(QStringLiteral("a keyform of %1 has the wrong number of vertices").arg(mesh.id));
			a.f32(MeshKeyformOpacity, mesh.opacities.empty() ? 1.0f : std::clamp(mesh.opacities[k], 0.0f, 1.0f));
			a.f32(MeshKeyformDrawOrder, kDrawOrder);
			a.i32(MeshKeyformPositionsBegin, positions); // in floats
			for (float value : xy)
				a.f32(PositionsXy, value);
			for (int extra = vertices; extra < stride; extra++) {
				a.f32(PositionsXy, 0);
				a.f32(PositionsXy, 0);
			}
			positions += 2 * stride;
		}
		keyforms += static_cast<int>(expected);
	}
	for (int s : {MeshScratch0, MeshScratch1, MeshScratch2, MeshScratch3})
		a.scratch(s, meshCount);

	// One draw order group: the meshes back to front.
	a.i32(GroupObjectsBegin, 0);
	a.i32(GroupObjectsCount, meshCount);
	a.i32(GroupObjectsTotal, meshCount);
	a.u32(GroupHighest, static_cast<uint32_t>(kDrawOrder));
	a.u32(GroupLowest, static_cast<uint32_t>(kDrawOrder));
	for (int m = 0; m < meshCount; m++) {
		a.u32(GroupObjectType, 0);
		a.i32(GroupObjectIndex, m);
		a.i32(GroupObjectSelf, -1);
	}

	a.counts[Parts] = 1;
	a.counts[ArtMeshes] = meshCount;
	a.counts[Parameters] = parameterCount;
	a.counts[PartKeyforms] = 1;
	a.counts[ArtMeshKeyforms] = keyforms;
	a.counts[KeyformPositions] = positions;
	a.counts[ParameterBindingIndices] = bindingIndices;
	a.counts[KeyformBindings] = keyformBindings;
	a.counts[ParameterBindings] = bindings;
	a.counts[Keys] = keys;
	a.counts[Uvs] = uvs;
	a.counts[PositionIndices] = indices;
	a.counts[DrawOrderGroups] = 1;
	a.counts[DrawOrderGroupObjects] = meshCount;

	// Every array as long as its count says.
	for (int f = 0; f < kArrayFields; f++)
		if (a.bytes[f].size() != static_cast<qsizetype>(a.counts[kFields[f].count]) * elementSize(kFields[f].kind))
			return fail(QStringLiteral("array %1 has %2 bytes, its count says %3")
					    .arg(f)
					    .arg(a.bytes[f].size())
					    .arg(a.counts[kFields[f].count] * elementSize(kFields[f].kind)));

	// The file: header, the table of offsets, scratch, the counts, the canvas, then the arrays, each on 64 bytes.
	QByteArray file(kDataStart, '\0');
	std::memcpy(file.data(), "MOC3", 4);
	file[4] = static_cast<char>(kVersion300);
	file[5] = 0; // little-endian
	std::vector<uint32_t> offsets;
	offsets.push_back(static_cast<uint32_t>(file.size()));
	for (uint32_t count : a.counts) {
		char raw[4];
		qToLittleEndian(count, raw);
		file.append(raw, 4);
	}
	file.append(QByteArray(128 - 4 * kCountFields, '\0'));
	pad(file, kAlign);
	offsets.push_back(static_cast<uint32_t>(file.size()));
	for (float value : {model.pixelsPerUnit, model.originX, model.originY, model.canvasWidth, model.canvasHeight}) {
		uint32_t bits;
		std::memcpy(&bits, &value, 4);
		char raw[4];
		qToLittleEndian(bits, raw);
		file.append(raw, 4);
	}
	file.append('\0'); // canvas flags
	pad(file, kAlign);
	for (int f = 0; f < kArrayFields; f++) {
		pad(file, kAlign);
		offsets.push_back(static_cast<uint32_t>(file.size()));
		file.append(a.bytes[f]);
	}
	pad(file, kAlign);
	for (size_t i = 0; i < offsets.size(); i++)
		qToLittleEndian(offsets[i], file.data() + 0x40 + 4 * i);
	return file;
}

Summary read(const QByteArray &file)
{
	Summary s;
	const auto fail = [&s](const QString &why) {
		s.ok = false;
		s.why = why;
		return s;
	};
	if (file.size() < kDataStart + 192 || !file.startsWith("MOC3"))
		return fail(QStringLiteral("not a .moc3"));
	s.version = static_cast<uint8_t>(file[4]);
	if (s.version != kVersion300 || file[5] != 0)
		return fail(QStringLiteral("not a little-endian 3.00 file"));
	if (file.size() % kAlign)
		return fail(QStringLiteral("not a multiple of 64 bytes long"));
	for (int i = 0; i < 2 + kArrayFields; i++)
		s.offsets.push_back(u32At(file, 0x40 + 4 * i));
	for (size_t i = 0; i < s.offsets.size(); i++) {
		if (s.offsets[i] % kAlign || s.offsets[i] < static_cast<uint32_t>(kDataStart) ||
		    s.offsets[i] > static_cast<uint32_t>(file.size()) || (i && s.offsets[i] < s.offsets[i - 1]))
			return fail(QStringLiteral("offset %1 is out of place").arg(i));
	}
	for (int i = 0; i < kCountFields; i++)
		s.counts.push_back(u32At(file, s.offsets[0] + 4 * i));
	for (int i = 0; i < 5; i++)
		s.canvas[i] = f32At(file, s.offsets[1] + 4 * i);
	for (int f = 0; f < kArrayFields; f++) {
		const qsizetype end = static_cast<qsizetype>(s.offsets[2 + f]) +
				      static_cast<qsizetype>(s.counts[kFields[f].count]) * elementSize(kFields[f].kind);
		if (end > file.size() || (f + 1 < kArrayFields && end > static_cast<qsizetype>(s.offsets[3 + f])))
			return fail(QStringLiteral("array %1 runs past where the next begins").arg(f));
	}
	const auto at = [&s](int slot) { return static_cast<qsizetype>(s.offsets[2 + slot]); };
	const auto ids = [&](int slot, uint32_t count) {
		QStringList out;
		for (uint32_t i = 0; i < count; i++)
			out << QString::fromLatin1(file.constData() + at(slot) + 64 * i);
		return out;
	};
	s.parameters = ids(ParameterIds, s.counts[Parameters]);
	s.meshes = ids(MeshIds, s.counts[ArtMeshes]);
	for (uint32_t k = 0; k < s.counts[Keys]; k++)
		s.keys.push_back(f32At(file, at(KeyValues) + 4 * k));
	for (uint32_t m = 0; m < s.counts[ArtMeshes]; m++) {
		const auto i32 = [&](int slot) { return static_cast<int32_t>(u32At(file, at(slot) + 4 * m)); };
		const int vertices = i32(MeshVertexCount), first = i32(MeshKeyformsBegin), uvBegin = i32(MeshUvsBegin);
		s.keyformCounts.push_back(i32(MeshKeyformsCount));
		if (first < 0 || static_cast<uint32_t>(first) >= s.counts[ArtMeshKeyforms] || uvBegin < 0 ||
		    static_cast<uint32_t>(uvBegin + 2 * vertices) > s.counts[Uvs])
			return fail(QStringLiteral("mesh %1 points past the arrays").arg(m));
		const int begin = static_cast<int32_t>(u32At(file, at(MeshKeyformPositionsBegin) + 4 * first));
		if (begin < 0 || static_cast<uint32_t>(begin + 2 * vertices) > s.counts[KeyformPositions])
			return fail(QStringLiteral("mesh %1's positions are past the arrays").arg(m));
		std::vector<float> xy, uv;
		for (int v = 0; v < 2 * vertices; v++) {
			xy.push_back(f32At(file, at(PositionsXy) + 4 * (begin + v)));
			uv.push_back(f32At(file, at(UvValues) + 4 * (uvBegin + v)));
		}
		s.firstKeyform.push_back(xy);
		s.uvs.push_back(uv);
	}
	s.ok = true;
	return s;
}

} // namespace sarxina::jobapplication::moc3
