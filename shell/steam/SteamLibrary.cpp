#include "SteamLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

Q_LOGGING_CATEGORY(lcSteam, "tvshell.steam")

namespace {

// appmanifest StateFlags bit for "fully installed".
constexpr int StateFullyInstalled = 4;

// Steam installs these like games; they aren't.
const QRegularExpression toolNames(
    QStringLiteral("^(Proton|Steam Linux Runtime|Steamworks Common Redistributables|SteamVR)"));

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

// Value of a top-level "key" "value" pair in a VDF/ACF file (good enough for manifests).
QString vdfValue(const QString &text, const QString &key)
{
    const QRegularExpression pattern(QStringLiteral("\"%1\"\\s+\"([^\"]*)\"").arg(QRegularExpression::escape(key)),
                                     QRegularExpression::CaseInsensitiveOption);
    return pattern.match(text).captured(1);
}

// Steam's portrait cover (600x900). Older entries keep it at the top of the app's cache folder;
// newer ones in a hashed subfolder, sometimes named library_capsule.jpg.
QUrl findCover(const QString &cacheDir)
{
    const QStringList names{QStringLiteral("library_600x900.jpg"), QStringLiteral("library_capsule.jpg")};
    for (const QString &name : names) {
        if (QFileInfo::exists(cacheDir + u'/' + name))
            return QUrl::fromLocalFile(cacheDir + u'/' + name);
    }
    const QFileInfoList subdirs = QDir(cacheDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QString &name : names) {
        for (const QFileInfo &subdir : subdirs) {
            const QString path = subdir.filePath() + u'/' + name;
            if (QFileInfo::exists(path))
                return QUrl::fromLocalFile(path);
        }
    }
    return {};
}

} // namespace

SteamLibrary::SteamLibrary(QObject *parent)
    : QAbstractListModel(parent)
    , m_steamRoot(QDir::homePath() + QStringLiteral("/.local/share/Steam"))
{
    reload();
}

int SteamLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_games.size());
}

QVariant SteamLibrary::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Game &game = m_games.at(index.row());
    switch (role) {
    case AppIdRole:
        return game.appId;
    case NameRole:
        return game.name;
    case CoverRole:
        return game.cover;
    default:
        return {};
    }
}

QHash<int, QByteArray> SteamLibrary::roleNames() const
{
    return {
        {AppIdRole, "appId"},
        {NameRole, "name"},
        {CoverRole, "cover"},
    };
}

QStringList SteamLibrary::libraryFolders() const
{
    QStringList folders;
    const QString vdf = readFile(m_steamRoot + QStringLiteral("/steamapps/libraryfolders.vdf"));
    static const QRegularExpression pathPattern(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
    for (auto it = pathPattern.globalMatch(vdf); it.hasNext();)
        folders << it.next().captured(1);
    if (folders.isEmpty())
        folders << m_steamRoot;
    folders.removeDuplicates();
    return folders;
}

void SteamLibrary::reload()
{
    QList<Game> games;
    for (const QString &folder : libraryFolders()) {
        const QDir steamapps(folder + QStringLiteral("/steamapps"));
        const QStringList manifests = steamapps.entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files);
        for (const QString &manifest : manifests) {
            const QString text = readFile(steamapps.filePath(manifest));
            Game game;
            game.appId = vdfValue(text, QStringLiteral("appid"));
            game.name = vdfValue(text, QStringLiteral("name"));
            const int state = vdfValue(text, QStringLiteral("StateFlags")).toInt();
            if (game.appId.isEmpty() || !(state & StateFullyInstalled) || toolNames.match(game.name).hasMatch())
                continue;
            game.cover = findCover(m_steamRoot + QStringLiteral("/appcache/librarycache/") + game.appId);
            games << game;
        }
    }
    // Stable order for now; "places" will own positions later.
    std::sort(games.begin(), games.end(), [](const Game &a, const Game &b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    beginResetModel();
    m_games = games;
    endResetModel();
    qCInfo(lcSteam) << "found" << m_games.size() << "installed games";
    emit countChanged();
}

void SteamLibrary::launch(const QString &appId) const
{
    qCInfo(lcSteam) << "launching" << appId;
    if (!QProcess::startDetached(QStringLiteral("steam"), {QStringLiteral("steam://rungameid/") + appId}))
        qCWarning(lcSteam) << "could not run steam";
}
