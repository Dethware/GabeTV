#include "SteamLog.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSteamLog, "tvshell.steamlog")

namespace {

constexpr int PollIntervalMs = 250;
// e.g. "[2026-11-21 16:46:24] QuickAccess_uid2-'QuickAcces': WasHidden 0: (2465, 80) 1717x1282"
constexpr QByteArrayView QuickAccessShown = "QuickAccess_uid2-'QuickAcces': WasHidden 0";
constexpr QByteArrayView QuickAccessHidden = "QuickAccess_uid2-'QuickAcces': WasHidden 1";

} // namespace

SteamLog::SteamLog(QObject *parent)
    : QObject(parent)
    , m_path(QDir::homePath() + QStringLiteral("/.local/share/Steam/logs/webhelper.txt"))
{
    m_timer.setInterval(PollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SteamLog::poll);
    m_timer.start();
    poll();
}

void SteamLog::poll()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const qint64 size = file.size();
    if (m_offset < 0) {
        // Start at the end: only what happens from now on matters.
        m_offset = size;
        return;
    }
    if (size < m_offset) {
        // Steam started a new log (restart).
        m_offset = 0;
        m_partial.clear();
    }
    if (size == m_offset || !file.seek(m_offset))
        return;

    const QByteArray chunk = file.readAll();
    m_offset += chunk.size();
    m_partial += chunk;
    qsizetype start = 0;
    for (qsizetype end = m_partial.indexOf('\n'); end >= 0; end = m_partial.indexOf('\n', start)) {
        handleLine(QByteArrayView(m_partial).sliced(start, end - start));
        start = end + 1;
    }
    m_partial.remove(0, start);
}

void SteamLog::handleLine(QByteArrayView line)
{
    if (line.contains(QuickAccessShown)) {
        qCInfo(lcSteamLog) << "Quick Access opened";
        emit quickAccessOpened();
    } else if (line.contains(QuickAccessHidden)) {
        qCInfo(lcSteamLog) << "Quick Access closed";
        emit quickAccessClosed();
    }
}
