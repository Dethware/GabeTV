#include "RomFs.h"

namespace SwitchFormat {

namespace {

constexpr quint32 None = 0xFFFFFFFF;

} // namespace

std::optional<RomFs> RomFs::open(const std::shared_ptr<ByteSource> &data)
{
    // Header (0x50): its own size, then offset and size of the directory hash table, the
    // directory table, the file hash table and the file table, then the offset of the files'
    // data. All u64, from the start of the RomFS.
    const QByteArray header = data->read(0, 0x50);
    if (header.size() != 0x50 || le64(header, 0) != 0x50)
        return std::nullopt;
    const qint64 dirTableOffset = qint64(le64(header, 0x18));
    const qint64 dirTableSize = qint64(le64(header, 0x20));
    const qint64 fileTableOffset = qint64(le64(header, 0x38));
    const qint64 fileTableSize = qint64(le64(header, 0x40));
    const qint64 dataOffset = qint64(le64(header, 0x48));
    if (dirTableSize < 0x18 || dirTableSize > 0x1000000 || fileTableSize > 0x1000000)
        return std::nullopt;

    // The root directory is the first entry of the directory table: parent, sibling, first
    // child directory, first file (u32 offsets into the file table), …
    const QByteArray rootDir = data->read(dirTableOffset, 0x18);
    const QByteArray files = data->read(fileTableOffset, fileTableSize);
    if (rootDir.size() != 0x18 || files.size() != fileTableSize)
        return std::nullopt;

    // A file entry: parent, next sibling (u32), data offset, size (u64), next in its hash
    // bucket, name length (u32), the name.
    RomFs fs;
    fs.m_data = data;
    quint32 at = le32(rootDir, 0xC);
    for (int guard = 0; at != None && guard < 0x10000; ++guard) {
        if (qint64(at) + 0x20 > files.size())
            return std::nullopt;
        const quint32 nameLength = le32(files, at + 0x1C);
        if (qint64(at) + 0x20 + nameLength > files.size())
            return std::nullopt;
        const QString name = QString::fromUtf8(files.constData() + at + 0x20, nameLength);
        fs.m_files.insert(name, {dataOffset + qint64(le64(files, at + 8)), qint64(le64(files, at + 0x10))});
        at = le32(files, at + 4);
    }
    return fs;
}

QByteArray RomFs::file(const QString &name) const
{
    const auto it = m_files.constFind(name);
    if (it == m_files.constEnd())
        return {};
    return m_data->read(it->offset, it->size);
}

} // namespace SwitchFormat
