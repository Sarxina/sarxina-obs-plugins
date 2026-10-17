#include "zip.hpp"

#include <zlib.h>

#include <cstdint>

namespace sarxina {

namespace {

constexpr qsizetype kMostFiles = 1000;
constexpr qint64 kMostBytes = 256ll << 20; // unpacked, all files together

constexpr uint32_t kEnd = 0x06054b50;     // end of the central directory
constexpr uint32_t kCentral = 0x02014b50; // a file in the central directory
constexpr uint32_t kLocal = 0x04034b50;   // a file's own header, before its data

// Little-endian numbers at `at`, or 0 past the end (the callers check the bounds first).
uint32_t u16(const QByteArray &zip, qsizetype at)
{
	if (at < 0 || at + 2 > zip.size())
		return 0;
	const auto *p = reinterpret_cast<const unsigned char *>(zip.constData() + at);
	return p[0] | p[1] << 8;
}

uint32_t u32(const QByteArray &zip, qsizetype at)
{
	if (at < 0 || at + 4 > zip.size())
		return 0;
	const auto *p = reinterpret_cast<const unsigned char *>(zip.constData() + at);
	return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

bool inflateRaw(const char *in, uint32_t inSize, QByteArray &out)
{
	z_stream stream{};
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
		return false;
	stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in));
	stream.avail_in = inSize;
	stream.next_out = reinterpret_cast<Bytef *>(out.data());
	stream.avail_out = static_cast<uInt>(out.size());
	const int result = inflate(&stream, Z_FINISH);
	const bool whole = result == Z_STREAM_END && stream.total_out == static_cast<uLong>(out.size());
	inflateEnd(&stream);
	return whole;
}

} // namespace

bool readZip(const QByteArray &zip, std::vector<ZipFile> &files, QString &problem)
{
	files.clear();
	const auto fail = [&problem, &files](const QString &why) {
		problem = why;
		files.clear();
		return false;
	};

	// The end record is the last 22 bytes, unless the zip has a comment after it.
	qsizetype end = -1;
	for (qsizetype at = zip.size() - 22; at >= 0 && at >= zip.size() - 22 - 0xFFFF; at--) {
		if (u32(zip, at) == kEnd && at + 22 + u16(zip, at + 20) == zip.size()) {
			end = at;
			break;
		}
	}
	if (end < 0)
		return fail(QStringLiteral("not a zip"));
	const uint32_t count = u16(zip, end + 10);
	const qint64 directorySize = u32(zip, end + 12), directory = u32(zip, end + 16);
	if (u16(zip, end + 4) != 0 || u16(zip, end + 6) != 0 || u16(zip, end + 8) != count)
		return fail(QStringLiteral("a zip in several parts"));
	if (count > kMostFiles)
		return fail(QStringLiteral("too many files"));
	if (directory + directorySize > end)
		return fail(QStringLiteral("a broken zip"));

	qint64 unpacked = 0;
	qsizetype at = directory;
	for (uint32_t i = 0; i < count; i++) {
		if (at + 46 > end || u32(zip, at) != kCentral)
			return fail(QStringLiteral("a broken zip"));
		const uint32_t flags = u16(zip, at + 8), method = u16(zip, at + 10), crc = u32(zip, at + 16);
		const uint32_t packedSize = u32(zip, at + 20), size = u32(zip, at + 24);
		const uint32_t nameLength = u16(zip, at + 28), extraLength = u16(zip, at + 30), commentLength = u16(zip, at + 32);
		const uint32_t local = u32(zip, at + 42);
		if (at + 46 + nameLength + extraLength + commentLength > end)
			return fail(QStringLiteral("a broken zip"));
		const QByteArray name = zip.mid(at + 46, nameLength);
		at += 46 + nameLength + extraLength + commentLength;

		for (const char c : name)
			if (c < 0x20 || c > 0x7E || c == '\\')
				return fail(QStringLiteral("a file name it cannot use"));
		if (flags & 1)
			return fail(QStringLiteral("an encrypted zip"));
		if (packedSize == 0xFFFFFFFF || size == 0xFFFFFFFF || local == 0xFFFFFFFF)
			return fail(QStringLiteral("a zip64 zip"));
		if (name.endsWith('/') && size == 0)
			continue; // a folder
		if (method != 0 && method != 8)
			return fail(QStringLiteral("a file packed in a way it cannot unpack"));
		unpacked += size;
		if (size > kMostBytes || unpacked > kMostBytes)
			return fail(QStringLiteral("too big"));

		// The file's own header, then its data. Its sizes are in the directory: the header may leave them out.
		if (u32(zip, local) != kLocal || local + 30 > directory)
			return fail(QStringLiteral("a broken zip"));
		const qint64 data = local + 30 + qint64(u16(zip, local + 26)) + u16(zip, local + 28);
		if (data + packedSize > directory)
			return fail(QStringLiteral("a broken zip"));

		ZipFile file{QString::fromLatin1(name), QByteArray()};
		if (method == 0) {
			if (packedSize != size)
				return fail(QStringLiteral("a broken zip"));
			file.data = zip.mid(data, size);
		} else {
			file.data.resize(size);
			if (!inflateRaw(zip.constData() + data, packedSize, file.data))
				return fail(QStringLiteral("a file that does not unpack"));
		}
		const uLong check = crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(file.data.constData()),
					  static_cast<uInt>(file.data.size()));
		if (check != crc)
			return fail(QStringLiteral("a file that is not what the zip says"));
		files.push_back(std::move(file));
	}
	return true;
}

} // namespace sarxina
