#pragma once

#include "Storage.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <memory>
#include <optional>

namespace SwitchFormat {

// A RomFS: a read-only file system with a directory and a file table. Only the files in its
// root are needed (control.nacp, icon_<Language>.dat).
class RomFs
{
public:
    // Reads the header and the file table; nullopt if it isn't a RomFS.
    static std::optional<RomFs> open(const std::shared_ptr<ByteSource> &data);

    bool contains(const QString &name) const { return m_files.contains(name); }
    // A file in the root, or empty.
    QByteArray file(const QString &name) const;

private:
    struct Location {
        qint64 offset;
        qint64 size;
    };

    std::shared_ptr<ByteSource> m_data;
    QHash<QString, Location> m_files; // root files by name
};

} // namespace SwitchFormat
