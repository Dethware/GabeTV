#include "SwitchGame.h"

#include "GameCache.h"
#include "format/GameFile.h"
#include "format/Keys.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QThread>

Q_LOGGING_CATEGORY(lcSwitch, "tvshell.switch")

namespace {

QString gamesRoot()
{
    return QDir::homePath() + QStringLiteral("/Games/Emulators/Switch");
}

// The languages the name and icon are taken in, most wanted first. The TV's language.
const QStringList Languages{QStringLiteral("BritishEnglish"), QStringLiteral("AmericanEnglish")};

} // namespace

SwitchGame::SwitchGame(QObject *parent)
    : QObject(parent)
{
}

SwitchGame::~SwitchGame()
{
    // The reader posts its result to this object: it has to be done before this goes.
    if (m_reader) {
        m_reader->wait();
        delete m_reader;
    }
}

void SwitchGame::setFile(const QString &file)
{
    if (m_file == file)
        return;
    m_file = file;
    emit fileChanged();
    load();
}

void SwitchGame::load()
{
    m_titleId.clear();
    m_name.clear();
    m_icon.clear();
    emit infoChanged();
    if (m_file.isEmpty())
        return;

    const QString path = QDir(gamesRoot()).absoluteFilePath(m_file);
    if (const auto cached = GameCache().find(QFileInfo(path))) {
        m_titleId = cached->titleId;
        m_name = cached->name;
        m_icon = cached->icon;
        emit infoChanged();
        return;
    }
    read(path);
}

void SwitchGame::read(const QString &path)
{
    if (m_reader) {
        m_loadWhenRead = true;
        return;
    }
    m_reader = QThread::create([this, path] {
        // The keys live only as long as this read.
        SwitchFormat::Keys keys;
        QString error;
        const QString keysRoot = gamesRoot() + QStringLiteral("/keys");
        const QString keysFolder = SwitchFormat::Keys::newestFolder(keysRoot);
        std::optional<GameCache::Game> game;
        if (keysFolder.isEmpty()) {
            error = QStringLiteral("no keys in ") + keysRoot;
        } else if (keys.load(keysFolder, &error)) {
            if (const auto info = SwitchFormat::readGame(path, keys, Languages, &error))
                game = GameCache().store(QFileInfo(path), *info);
        }
        QMetaObject::invokeMethod(this, [this, path, game, error] {
            if (m_loadWhenRead)
                return; // for a file that is no longer this one
            if (!game) {
                qCWarning(lcSwitch).noquote() << path << "can't be read:" << error;
                return;
            }
            qCInfo(lcSwitch).noquote() << "Read" << game->name << "from" << path;
            m_titleId = game->titleId;
            m_name = game->name;
            m_icon = game->icon;
            emit infoChanged();
        }, Qt::QueuedConnection);
    });
    connect(m_reader, &QThread::finished, this, [this] {
        m_reader->deleteLater();
        m_reader = nullptr;
        if (m_loadWhenRead) {
            m_loadWhenRead = false;
            load();
        }
    });
    m_reader->start(QThread::LowPriority);
}
