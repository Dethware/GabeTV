#include "Nca.h"

#include "Aes.h"
#include "Keys.h"

namespace SwitchFormat {

namespace {

constexpr qint64 HeaderSize = 0xC00;
constexpr qint64 BlockSize = 0x200; // the unit of the section table

// FsHeader encryption types.
constexpr quint8 EncryptionNone = 1;
constexpr quint8 EncryptionAesCtr = 3;
constexpr quint8 EncryptionAesCtrSkipLayerHash = 5;

// FsHeader hash types.
constexpr quint8 HierarchicalSha256 = 2;
constexpr quint8 HierarchicalIntegrity = 3;

// A part of an NCA, decrypted with AES-CTR as it is read (or read as it is, without a key).
class SectionData : public ByteSource
{
public:
    SectionData(std::shared_ptr<FileRange> nca, qint64 start, qint64 size, QByteArray key, quint64 counter)
        : m_nca(std::move(nca))
        , m_start(start)
        , m_size(size)
        , m_key(std::move(key))
        , m_counter(counter)
    {
    }

    qint64 size() const override { return m_size; }

    QByteArray read(qint64 offset, qint64 length) const override
    {
        if (offset < 0 || length < 0 || offset >= m_size)
            return {};
        length = qMin(length, m_size - offset);
        const qint64 at = m_start + offset; // in the NCA
        if (m_key.isEmpty())
            return m_nca->read(at, length);
        const qint64 aligned = at & ~qint64(15);
        const QByteArray encrypted = m_nca->read(aligned, length + (at - aligned));
        if (encrypted.size() != length + (at - aligned))
            return {};
        return Aes::decryptCtr(m_key, encrypted, m_counter, aligned).mid(at - aligned);
    }

private:
    std::shared_ptr<FileRange> m_nca;
    qint64 m_start;
    qint64 m_size;
    QByteArray m_key; // empty: not encrypted
    quint64 m_counter;
};

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

} // namespace

std::optional<Nca> Nca::open(const std::shared_ptr<FileRange> &data, const Keys &keys, QString *error)
{
    if (keys.headerKey().size() != 32) {
        setError(error, QStringLiteral("header_key is missing from prod.keys"));
        return std::nullopt;
    }
    const QByteArray encrypted = data->read(0, HeaderSize);
    if (encrypted.size() != HeaderSize) {
        setError(error, QStringLiteral("too short for an NCA"));
        return std::nullopt;
    }
    // NCA3: all of the header is one run of XTS sectors from 0. (NCA2, from before the
    // Switch was sold, encrypts each section header as sector 0; not supported.)
    Nca nca;
    nca.m_header = Aes::decryptXts(keys.headerKey(), encrypted, 0);
    if (nca.m_header.size() != HeaderSize || nca.m_header.mid(0x200, 4) != "NCA3") {
        setError(error, QStringLiteral("not an NCA3 (or a wrong header_key)"));
        return std::nullopt;
    }
    nca.m_data = data;
    nca.m_contentType = ContentType(quint8(nca.m_header[0x205]));
    nca.m_keyGeneration = qMax(quint8(nca.m_header[0x206]), quint8(nca.m_header[0x220]));
    nca.m_titleId = le64(nca.m_header, 0x210);
    nca.m_rightsId = nca.m_header.mid(0x230, 16);
    return nca;
}

bool Nca::hasRightsId() const
{
    return m_rightsId.count('\0') != m_rightsId.size();
}

std::shared_ptr<ByteSource> Nca::romFs(int section, const Keys &keys, QString *error) const
{
    if (section < 0 || section > 3) {
        setError(error, QStringLiteral("no section %1").arg(section));
        return {};
    }
    // The section table at 0x240: start and end in blocks of 0x200, from the NCA's start.
    const qint64 sectionStart = qint64(le32(m_header, 0x240 + section * 0x10)) * BlockSize;
    const qint64 sectionEnd = qint64(le32(m_header, 0x240 + section * 0x10 + 4)) * BlockSize;
    if (sectionEnd <= sectionStart || sectionEnd > m_data->size()) {
        setError(error, QStringLiteral("section %1 is empty or outside the NCA").arg(section));
        return {};
    }

    const QByteArray fs = m_header.mid(0x400 + section * 0x200, 0x200);
    if (fs[2] != 0) {
        setError(error, QStringLiteral("section %1 isn't a RomFS").arg(section));
        return {};
    }

    // Where the RomFS itself is in the section: the last level of its hash tree. The hash
    // data starts at 0x8 of the section's header.
    qint64 dataOffset = 0;
    qint64 dataSize = 0;
    const quint8 hashType = quint8(fs[3]);
    if (hashType == HierarchicalIntegrity && fs.mid(0x8, 4) == "IVFC") {
        // IVFC: MaxLayers at 0x14, then the levels from 0x18, 0x18 bytes each (offset, size,
        // log2 of the block size); the master hash is not a level, so MaxLayers − 1 of them.
        const quint32 layers = le32(fs, 0x14);
        if (layers < 2 || layers > 7) {
            setError(error, QStringLiteral("section %1: %2 hash levels").arg(section).arg(layers));
            return {};
        }
        const qsizetype level = 0x18 + (layers - 2) * 0x18;
        dataOffset = qint64(le64(fs, level));
        dataSize = qint64(le64(fs, level + 8));
    } else if (hashType == HierarchicalSha256) {
        // Layer count at 0x2C, then regions (offset, size) from 0x30; the last is the data.
        const quint32 layers = le32(fs, 0x2C);
        if (layers < 1 || layers > 5) {
            setError(error, QStringLiteral("section %1: %2 hash layers").arg(section).arg(layers));
            return {};
        }
        const qsizetype region = 0x30 + (layers - 1) * 0x10;
        dataOffset = qint64(le64(fs, region));
        dataSize = qint64(le64(fs, region + 8));
    } else {
        setError(error, QStringLiteral("section %1: hash type %2 isn't supported").arg(section).arg(hashType));
        return {};
    }
    if (dataOffset < 0 || dataSize <= 0 || dataOffset + dataSize > sectionEnd - sectionStart) {
        setError(error, QStringLiteral("section %1: the RomFS is outside the section").arg(section));
        return {};
    }

    const quint8 encryption = quint8(fs[4]);
    if (encryption == EncryptionNone)
        return std::make_shared<SectionData>(m_data, sectionStart + dataOffset, dataSize, QByteArray(), 0);
    if (encryption != EncryptionAesCtr && encryption != EncryptionAesCtrSkipLayerHash) {
        setError(error, QStringLiteral("section %1: encryption type %2 isn't supported").arg(section).arg(encryption));
        return {};
    }

    // The section key: with a rights id, the title key decrypted with titlekek; without, the
    // key area at 0x300 (four keys) decrypted with the key area key, of which the third is
    // AES-CTR's.
    QByteArray key;
    if (hasRightsId()) {
        const QByteArray titleKey = keys.titleKey(m_rightsId);
        if (titleKey.size() != 16) {
            setError(error, QStringLiteral("no title key for rights id %1").arg(QString::fromLatin1(m_rightsId.toHex())));
            return {};
        }
        if (keys.titleKek(m_keyGeneration).size() != 16) {
            setError(error, QStringLiteral("titlekek for key generation %1 is missing").arg(m_keyGeneration));
            return {};
        }
        key = Aes::decryptEcb(keys.titleKek(m_keyGeneration), titleKey);
    } else {
        if (m_header[0x207] != 0) {
            setError(error, QStringLiteral("key area index %1 isn't supported (only application)").arg(int(m_header[0x207])));
            return {};
        }
        if (keys.keyAreaKey(m_keyGeneration).size() != 16) {
            setError(error, QStringLiteral("key_area_key_application for key generation %1 is missing").arg(m_keyGeneration));
            return {};
        }
        key = Aes::decryptEcb(keys.keyAreaKey(m_keyGeneration), m_header.mid(0x300, 0x40)).mid(0x20, 16);
    }
    if (key.size() != 16) {
        setError(error, QStringLiteral("section %1: the key couldn't be decrypted").arg(section));
        return {};
    }

    // The counter's upper half: the u64 at 0x140 (generation and secure value), big-endian.
    return std::make_shared<SectionData>(m_data, sectionStart + dataOffset, dataSize, key, le64(fs, 0x140));
}

} // namespace SwitchFormat
