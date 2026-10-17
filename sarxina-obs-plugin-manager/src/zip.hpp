// Reads a zip file held in memory, as the plugins' release zips are made: files stored or deflated (unpacked with
// zlib, which OBS ships), each one's CRC checked. Folder entries are skipped. Encrypted files, zip64 and other
// methods are refused, and so is a zip that holds more than 1000 files or unpacks to more than 256 MB.
#pragma once

#include <QByteArray>
#include <QString>

#include <vector>

namespace sarxina {

struct ZipFile {
	QString name; // as in the zip: "obs-plugins/64bit/slinglive.dll"
	QByteArray data;
};

// False, with why in a few words, when the zip cannot be read whole.
bool readZip(const QByteArray &zip, std::vector<ZipFile> &files, QString &problem);

} // namespace sarxina
