#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

namespace SwitchFormat {

// The keys a game file needs, read from the user's key files. Values are kept in memory only
// and never written or logged anywhere; errors name a key, never its value.
class Keys
{
public:
    // The newest version folder in `root` by version number (~/Games/Emulators/Switch/keys
    // holds one per firmware, e.g. 22.5.0/), or empty if there is none.
    static QString newestFolder(const QString &root);

    // prod.keys from `folder`, and title.keys if it is there. False with `error` set when
    // prod.keys can't be read.
    bool load(const QString &folder, QString *error);

    // header_key: 32 bytes, the two halves of the NCA header's XTS key. Empty if missing.
    QByteArray headerKey() const;
    // key_area_key_application_NN for a key generation as an NCA states it (16 bytes).
    QByteArray keyAreaKey(int generation) const;
    // titlekek_NN for a key generation (16 bytes).
    QByteArray titleKek(int generation) const;
    // The still encrypted title key for a rights id (16 bytes each), from title.keys or a
    // ticket added with addTitleKey().
    QByteArray titleKey(const QByteArray &rightsId) const;

    // A title key from an NSP's own ticket.
    void addTitleKey(const QByteArray &rightsId, const QByteArray &encryptedKey);

private:
    // The NN in the key names: generations 0, 1 and 2 all use 00, later ones generation − 1.
    static int keyIndex(int generation);

    QHash<QString, QByteArray> m_keys;       // prod.keys, by name
    QHash<QByteArray, QByteArray> m_titleKeys; // by rights id
};

} // namespace SwitchFormat
