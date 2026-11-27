#pragma once

#include "Storage.h"

#include <QByteArray>
#include <QString>

#include <memory>
#include <optional>

namespace SwitchFormat {

class Keys;

// One NCA (a piece of a game: the program, its control data, the manual, …). Its first 0xC00
// bytes are a header encrypted with the header key; its sections are encrypted with a key
// from the header's key area or, for a title with a rights id, with the title key.
class Nca
{
public:
    enum class ContentType {
        Program = 0,
        Meta = 1,
        Control = 2,
        Manual = 3,
        Data = 4,
        PublicData = 5,
    };

    // Decrypts and checks the header (NCA3). nullopt with `error` set if the header key is
    // missing or the data isn't an NCA.
    static std::optional<Nca> open(const std::shared_ptr<FileRange> &data, const Keys &keys,
                                   QString *error);

    ContentType contentType() const { return m_contentType; }
    quint64 titleId() const { return m_titleId; }
    // 16 bytes, all zero when the NCA has none (cartridge games).
    QByteArray rightsId() const { return m_rightsId; }
    bool hasRightsId() const;

    // The data of a RomFS section (index 0 in a control NCA), decrypted as it is read: the
    // last level under its IVFC hash tree, where the RomFS itself starts. Null with `error`
    // set if the key is missing or the section isn't a RomFS encrypted with AES-CTR or not
    // at all.
    std::shared_ptr<ByteSource> romFs(int section, const Keys &keys, QString *error) const;

private:
    std::shared_ptr<FileRange> m_data;
    QByteArray m_header; // decrypted, 0xC00 bytes
    ContentType m_contentType = ContentType::Program;
    quint64 m_titleId = 0;
    int m_keyGeneration = 0;
    QByteArray m_rightsId;
};

} // namespace SwitchFormat
