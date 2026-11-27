#include "GameFile.h"

#include "Keys.h"
#include "Nacp.h"
#include "Nca.h"
#include "PartitionFs.h"
#include "RomFs.h"

namespace SwitchFormat {

namespace {

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

// An NSP's tickets (*.tik) give their title keys to `keys`. A ticket starts with its
// signature type, which says how long the signature before the ticket's data is; in the
// data the title key block is at 0x40, the key type at 0x141 (0 common, 1 made for one
// console, which isn't supported) and the rights id at 0x160.
void addTickets(const PartitionFs &nsp, Keys &keys)
{
    for (const PartitionFs::Entry &entry : nsp.entries()) {
        if (!entry.name.endsWith(QLatin1String(".tik")))
            continue;
        const QByteArray ticket = entry.data->read(0, 0x400);
        if (ticket.size() < 4)
            continue;
        qsizetype dataStart = 0;
        switch (le32(ticket, 0)) {
        case 0x10000: // RSA-4096 with SHA-1
        case 0x10003: // RSA-4096 with SHA-256
            dataStart = 4 + 0x200 + 0x3C;
            break;
        case 0x10001: // RSA-2048 with SHA-1
        case 0x10004: // RSA-2048 with SHA-256
            dataStart = 4 + 0x100 + 0x3C;
            break;
        case 0x10002: // ECDSA with SHA-1
        case 0x10005: // ECDSA with SHA-256
            dataStart = 4 + 0x3C + 0x40;
            break;
        default:
            continue;
        }
        if (ticket.size() < dataStart + 0x170 || ticket[dataStart + 0x141] != 0)
            continue;
        keys.addTitleKey(ticket.mid(dataStart + 0x160, 16), ticket.mid(dataStart + 0x40, 16));
    }
}

} // namespace

std::optional<GameInfo> readGame(const QString &path, Keys &keys, const QStringList &languages,
                                 QString *error)
{
    const auto file = FileRange::open(path);
    if (!file) {
        setError(error, QStringLiteral("can't be opened"));
        return std::nullopt;
    }

    // 1. The game's NCAs: an XCI's secure partition, or the NSP itself.
    std::optional<PartitionFs> ncas = readXciSecurePartition(file);
    if (!ncas) {
        ncas = PartitionFs::read(file);
        if (!ncas) {
            setError(error, QStringLiteral("neither an XCI nor an NSP"));
            return std::nullopt;
        }
        addTickets(*ncas, keys);
    }

    // 2. The control NCA: the one whose header says so. The others are opened only far
    //    enough to read that (the header, 0xC00 bytes).
    std::optional<Nca> control;
    QString ncaError;
    for (const PartitionFs::Entry &entry : ncas->entries()) {
        if (!entry.name.endsWith(QLatin1String(".nca")) || entry.name.endsWith(QLatin1String(".cnmt.nca")))
            continue;
        auto nca = Nca::open(entry.data, keys, &ncaError);
        if (nca && nca->contentType() == Nca::ContentType::Control) {
            control = std::move(nca);
            break;
        }
    }
    if (!control) {
        setError(error, ncaError.isEmpty() ? QStringLiteral("no control NCA") : QStringLiteral("no control NCA (") + ncaError + u')');
        return std::nullopt;
    }

    // 3. Its RomFS: control.nacp and the icons.
    const auto section = control->romFs(0, keys, error);
    if (!section)
        return std::nullopt;
    const auto romFs = RomFs::open(section);
    if (!romFs) {
        setError(error, QStringLiteral("the control NCA's RomFS can't be read (a wrong key?)"));
        return std::nullopt;
    }
    const auto nacp = Nacp::parse(romFs->file(QStringLiteral("control.nacp")));
    if (!nacp) {
        setError(error, QStringLiteral("control.nacp is missing or can't be read"));
        return std::nullopt;
    }

    GameInfo info;
    info.titleId = control->titleId();
    const Nacp::Title title = nacp->title(languages);
    info.name = title.name;
    info.publisher = title.publisher;
    info.version = nacp->displayVersion;
    for (const QString &language : languages + languageNames()) {
        const QString name = QStringLiteral("icon_%1.dat").arg(language);
        if (romFs->contains(name)) {
            info.icon = romFs->file(name);
            break;
        }
    }
    return info;
}

} // namespace SwitchFormat
