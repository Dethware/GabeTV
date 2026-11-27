#pragma once

#include "Storage.h"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace SwitchFormat {

// A plain file list: HFS0 (the partitions of an XCI, and each partition's files) or PFS0 (an
// NSP). A header with the entries and their names, then the files' data. Not encrypted.
class PartitionFs
{
public:
    struct Entry {
        QString name;
        std::shared_ptr<FileRange> data;
    };

    // Reads the list at the start of `range`; nullopt if it is neither HFS0 nor PFS0.
    static std::optional<PartitionFs> read(const std::shared_ptr<FileRange> &range);

    const QList<Entry> &entries() const { return m_entries; }
    // The entry with that name, or null.
    std::shared_ptr<FileRange> file(const QString &name) const;

private:
    QList<Entry> m_entries;
};

// The XCI's root list (from its header at 0x100) and in it the "secure" partition, which
// holds the game's NCAs. nullopt if `file` isn't an XCI.
std::optional<PartitionFs> readXciSecurePartition(const std::shared_ptr<FileRange> &file);

} // namespace SwitchFormat
