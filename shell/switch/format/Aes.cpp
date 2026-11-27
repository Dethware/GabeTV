#include "Aes.h"

#include <QtEndian>

#include <openssl/evp.h>

#include <memory>

namespace SwitchFormat::Aes {

namespace {

// One run of an OpenSSL cipher without padding; empty on any error.
QByteArray decrypt(const EVP_CIPHER *cipher, const QByteArray &key, const unsigned char *iv, const char *data,
                   int length)
{
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context || key.size() != EVP_CIPHER_get_key_length(cipher))
        return {};
    if (EVP_DecryptInit_ex(context.get(), cipher, nullptr, reinterpret_cast<const unsigned char *>(key.constData()), iv) != 1)
        return {};
    EVP_CIPHER_CTX_set_padding(context.get(), 0);
    QByteArray out(length + EVP_CIPHER_get_block_size(cipher), Qt::Uninitialized);
    int written = 0;
    int last = 0;
    auto *outData = reinterpret_cast<unsigned char *>(out.data());
    if (EVP_DecryptUpdate(context.get(), outData, &written, reinterpret_cast<const unsigned char *>(data), length) != 1
        || EVP_DecryptFinal_ex(context.get(), outData + written, &last) != 1)
        return {};
    out.truncate(written + last);
    return out;
}

} // namespace

QByteArray decryptXts(const QByteArray &key, const QByteArray &data, quint64 firstSector, int sectorSize)
{
    if (sectorSize <= 0 || data.size() % sectorSize != 0)
        return {};
    QByteArray out;
    out.reserve(data.size());
    for (qsizetype at = 0; at < data.size(); at += sectorSize) {
        // OpenSSL takes the tweak as the IV; ours is the sector number in its last 8 bytes.
        unsigned char tweak[16] = {};
        qToBigEndian<quint64>(firstSector + at / sectorSize, tweak + 8);
        const QByteArray sector = decrypt(EVP_aes_128_xts(), key, tweak, data.constData() + at, sectorSize);
        if (sector.size() != sectorSize)
            return {};
        out += sector;
    }
    return out;
}

QByteArray decryptEcb(const QByteArray &key, const QByteArray &data)
{
    if (data.size() % 16 != 0)
        return {};
    return decrypt(EVP_aes_128_ecb(), key, nullptr, data.constData(), int(data.size()));
}

QByteArray decryptCtr(const QByteArray &key, const QByteArray &data, quint64 sectionCounter, qint64 offset)
{
    if (offset % 16 != 0)
        return {};
    unsigned char counter[16];
    qToBigEndian<quint64>(sectionCounter, counter);
    qToBigEndian<quint64>(quint64(offset) / 16, counter + 8);
    return decrypt(EVP_aes_128_ctr(), key, counter, data.constData(), int(data.size()));
}

} // namespace SwitchFormat::Aes
