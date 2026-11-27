#include "PartitionFs.h"

namespace SwitchFormat {

std::optional<PartitionFs> PartitionFs::read(const std::shared_ptr<FileRange> &range)
{
    // Header: magic, file count, string table size, reserved; then the entries, then the
    // names. HFS0 entries are 0x40 bytes (with a hash), PFS0 entries 0x18. Each entry: data
    // offset (u64, from the end of the header), size (u64), name offset (u32).
    const QByteArray head = range->read(0, 0x10);
    if (head.size() != 0x10)
        return std::nullopt;
    qsizetype entrySize = 0;
    if (head.startsWith("HFS0"))
        entrySize = 0x40;
    else if (head.startsWith("PFS0"))
        entrySize = 0x18;
    else
        return std::nullopt;

    const quint32 count = le32(head, 4);
    const quint32 stringsSize = le32(head, 8);
    if (count > 0x10000 || stringsSize > 0x100000)
        return std::nullopt;
    const qint64 headerSize = 0x10 + qint64(count) * entrySize + stringsSize;
    const QByteArray header = range->read(0, headerSize);
    if (header.size() != headerSize)
        return std::nullopt;
    const QByteArray strings = header.mid(0x10 + qsizetype(count) * entrySize);

    PartitionFs fs;
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype at = 0x10 + qsizetype(i) * entrySize;
        const quint64 offset = le64(header, at);
        const quint64 size = le64(header, at + 8);
        const quint32 nameOffset = le32(header, at + 16);
        if (nameOffset >= quint32(strings.size()) || headerSize + offset + size > quint64(range->size()))
            return std::nullopt;
        const QString name = QString::fromUtf8(strings.constData() + nameOffset); // up to its NUL
        fs.m_entries.append({name, range->part(headerSize + qint64(offset), qint64(size))});
    }
    return fs;
}

std::shared_ptr<FileRange> PartitionFs::file(const QString &name) const
{
    for (const Entry &entry : m_entries) {
        if (entry.name == name)
            return entry.data;
    }
    return {};
}

std::optional<PartitionFs> readXciSecurePartition(const std::shared_ptr<FileRange> &file)
{
    // The cartridge header starts at 0x100 with "HEAD"; at 0x130 the root HFS0's offset in
    // the file and at 0x138 the size of its header.
    const QByteArray header = file->read(0x100, 0x40);
    if (header.size() != 0x40 || !header.startsWith("HEAD"))
        return std::nullopt;
    const quint64 rootOffset = le64(header, 0x30);
    if (rootOffset >= quint64(file->size()))
        return std::nullopt;
    const auto root = PartitionFs::read(file->part(qint64(rootOffset), file->size() - qint64(rootOffset)));
    if (!root)
        return std::nullopt;
    const auto secure = root->file(QStringLiteral("secure"));
    if (!secure)
        return std::nullopt;
    return PartitionFs::read(secure);
}

} // namespace SwitchFormat
