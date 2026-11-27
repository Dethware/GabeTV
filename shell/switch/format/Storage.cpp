#include "Storage.h"

namespace SwitchFormat {

std::shared_ptr<FileRange> FileRange::open(const QString &path)
{
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file->size();
    return std::make_shared<FileRange>(std::move(file), 0, size);
}

FileRange::FileRange(std::shared_ptr<QFile> file, qint64 offset, qint64 size)
    : m_file(std::move(file))
    , m_offset(offset)
    , m_size(size)
{
}

QByteArray FileRange::read(qint64 offset, qint64 length) const
{
    if (offset < 0 || length < 0 || offset >= m_size)
        return {};
    length = qMin(length, m_size - offset);
    if (!m_file->seek(m_offset + offset))
        return {};
    QByteArray data = m_file->read(length);
    return data.size() == length ? data : QByteArray();
}

std::shared_ptr<FileRange> FileRange::part(qint64 offset, qint64 size) const
{
    return std::make_shared<FileRange>(m_file, m_offset + offset, size);
}

} // namespace SwitchFormat
