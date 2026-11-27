#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QtEndian>

#include <memory>

// Reading the Switch's file formats. Written from the descriptions on switchbrew.org (pages
// XCI, Gamecard Format, NCA, RomFS, NACP, Ticket), not from any emulator's code.
namespace SwitchFormat {

// Little-endian numbers in a block of bytes; the caller has checked the block is long enough.
inline quint16 le16(const QByteArray &data, qsizetype at) { return qFromLittleEndian<quint16>(data.constData() + at); }
inline quint32 le32(const QByteArray &data, qsizetype at) { return qFromLittleEndian<quint32>(data.constData() + at); }
inline quint64 le64(const QByteArray &data, qsizetype at) { return qFromLittleEndian<quint64>(data.constData() + at); }

// Bytes that can be read by offset: a part of a file, or a decrypted NCA section.
class ByteSource
{
public:
    virtual ~ByteSource() = default;
    virtual qint64 size() const = 0;
    // `length` bytes from `offset`, or fewer at the end; empty on a read error.
    virtual QByteArray read(qint64 offset, qint64 length) const = 0;
};

// A range of a file on disk. Parts share the open file and its position, so a file and its
// parts are read from one thread at a time.
class FileRange : public ByteSource
{
public:
    // The whole file, or nothing if it can't be opened.
    static std::shared_ptr<FileRange> open(const QString &path);

    FileRange(std::shared_ptr<QFile> file, qint64 offset, qint64 size);

    qint64 size() const override { return m_size; }
    QByteArray read(qint64 offset, qint64 length) const override;

    // A part of this range.
    std::shared_ptr<FileRange> part(qint64 offset, qint64 size) const;

private:
    std::shared_ptr<QFile> m_file;
    qint64 m_offset;
    qint64 m_size;
};

} // namespace SwitchFormat
