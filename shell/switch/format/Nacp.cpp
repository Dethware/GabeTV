#include "Nacp.h"

#include "Storage.h"

#include <zlib.h>

namespace SwitchFormat {

namespace {

constexpr qsizetype NacpSize = 0x4000;
constexpr qsizetype TitleSize = 0x300;     // name 0x200, publisher 0x100
constexpr qsizetype MaxTitlesSize = 0x3000; // 16 titles, or the compressed ones

// A string field: UTF-8 up to its first NUL.
QString field(const QByteArray &data, qsizetype at, qsizetype size)
{
    const QByteArray bytes = data.mid(at, size);
    const qsizetype end = bytes.indexOf('\0');
    return QString::fromUtf8(end < 0 ? bytes : bytes.left(end));
}

// Deflate, without (windowBits −15) or with a zlib header (15). Empty on an error.
QByteArray inflate(const QByteArray &data, int windowBits)
{
    z_stream stream{};
    if (inflateInit2(&stream, windowBits) != Z_OK)
        return {};
    QByteArray out(TitleSize * 32, Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const int result = ::inflate(&stream, Z_FINISH);
    out.truncate(out.size() - stream.avail_out);
    inflateEnd(&stream);
    return result == Z_STREAM_END ? out : QByteArray();
}

} // namespace

const QStringList &languageNames()
{
    static const QStringList names{
        QStringLiteral("AmericanEnglish"),
        QStringLiteral("BritishEnglish"),
        QStringLiteral("Japanese"),
        QStringLiteral("French"),
        QStringLiteral("German"),
        QStringLiteral("LatinAmericanSpanish"),
        QStringLiteral("Spanish"),
        QStringLiteral("Italian"),
        QStringLiteral("Dutch"),
        QStringLiteral("CanadianFrench"),
        QStringLiteral("Portuguese"),
        QStringLiteral("Russian"),
        QStringLiteral("Korean"),
        QStringLiteral("TraditionalChinese"),
        QStringLiteral("SimplifiedChinese"),
        QStringLiteral("BrazilianPortuguese"),
        QStringLiteral("Polish"),
        QStringLiteral("Thai"),
    };
    return names;
}

std::optional<Nacp> Nacp::parse(const QByteArray &data)
{
    if (data.size() < NacpSize)
        return std::nullopt;

    // The titles: up to 16 at the start; since 21.0.0, when the byte at 0x3215 is 1, a
    // deflated block in the same place instead (its size as u16, then the data), which
    // inflates to titles of the same layout and can hold the newer languages.
    QByteArray titles = data.left(MaxTitlesSize);
    if (data[0x3215] == 1) {
        const QByteArray compressed = data.mid(2, qMin<qsizetype>(le16(data, 0), MaxTitlesSize - 2));
        titles = inflate(compressed, -15);
        if (titles.isEmpty())
            titles = inflate(compressed, 15);
        if (titles.isEmpty())
            return std::nullopt;
    }

    Nacp nacp;
    for (qsizetype at = 0; at + TitleSize <= titles.size() && nacp.titles.size() < languageNames().size(); at += TitleSize)
        nacp.titles.append({field(titles, at, 0x200), field(titles, at + 0x200, 0x100)});
    nacp.displayVersion = field(data, 0x3060, 0x10);
    return nacp;
}

Nacp::Title Nacp::title(const QStringList &languages) const
{
    for (const QString &language : languages) {
        const qsizetype index = languageNames().indexOf(language);
        if (index >= 0 && index < titles.size() && !titles[index].name.isEmpty())
            return titles[index];
    }
    for (const Title &title : titles) {
        if (!title.name.isEmpty())
            return title;
    }
    return {};
}

} // namespace SwitchFormat
