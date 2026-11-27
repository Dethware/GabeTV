#pragma once

#include <QByteArray>

// The AES modes the formats use, over OpenSSL's libcrypto. Keys are 16 bytes (XTS: 32).
namespace SwitchFormat::Aes {

// AES-128-XTS decryption of whole sectors. The tweak is the sector number big-endian,
// counted from `firstSector` (Nintendo's way; standard XTS counts little-endian).
QByteArray decryptXts(const QByteArray &key, const QByteArray &data, quint64 firstSector,
                      int sectorSize = 0x200);

// AES-128-ECB, without padding: the NCA's key area and title keys.
QByteArray decryptEcb(const QByteArray &key, const QByteArray &data);

// AES-128-CTR over data that starts at byte `offset` of the NCA (not of the section). The
// counter is the section's 8-byte value (from its NCA header) followed by offset / 16, both
// big-endian; `offset` must be a multiple of 16.
QByteArray decryptCtr(const QByteArray &key, const QByteArray &data, quint64 sectionCounter,
                      qint64 offset);

} // namespace SwitchFormat::Aes
