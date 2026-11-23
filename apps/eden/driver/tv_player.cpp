// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDBusConnection>
#include <QDBusMessage>
#include <QVariantMap>

#include "common/logging.h"
#include "yuzu/tv_player.h"

namespace {

const QString ObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString ServiceName = QStringLiteral("org.mpris.MediaPlayer2.eden");
constexpr int CheckMs = 200;

} // namespace

TvPlayer::TvPlayer(Hooks hooks_, QObject* parent) : QObject(parent), hooks{std::move(hooks_)} {
    new TvPlayerAdaptor(this);
    new TvPlayerRootAdaptor(this);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(ObjectPath, this) || !bus.registerService(ServiceName)) {
        LOG_WARNING(Frontend, "The TV shell can't reach this Eden: no {} on the session bus",
                    ServiceName.toStdString());
    }
    timer.setInterval(CheckMs);
    connect(&timer, &QTimer::timeout, this, &TvPlayer::Check);
    timer.start();
}

QString TvPlayer::PlaybackStatus() const {
    switch (state) {
    case State::Playing:
        return QStringLiteral("Playing");
    case State::Paused:
        return QStringLiteral("Paused");
    case State::Stopped:
        break;
    }
    return QStringLiteral("Stopped");
}

void TvPlayer::Pause() {
    hooks.pause();
    Check();
}

void TvPlayer::Play() {
    hooks.play();
    Check();
}

void TvPlayer::Check() {
    const State now = hooks.state();
    if (now == state) {
        return;
    }
    state = now;
    LOG_INFO(Frontend, "The game is now {}", PlaybackStatus().toStdString());
    hooks.changed(state);

    QDBusMessage changed = QDBusMessage::createSignal(
        ObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    changed << QStringLiteral("org.mpris.MediaPlayer2.Player")
            << QVariantMap{{QStringLiteral("PlaybackStatus"), PlaybackStatus()}} << QStringList{};
    QDBusConnection::sessionBus().send(changed);
}

TvPlayerAdaptor::TvPlayerAdaptor(TvPlayer* player_)
    : QDBusAbstractAdaptor(player_), player{player_} {}

QString TvPlayerAdaptor::PlaybackStatus() const {
    return player->PlaybackStatus();
}

void TvPlayerAdaptor::Pause() {
    player->Pause();
}

void TvPlayerAdaptor::Play() {
    player->Play();
}

void TvPlayerAdaptor::PlayPause() {
    if (player->PlaybackStatus() == QStringLiteral("Playing")) {
        player->Pause();
    } else {
        player->Play();
    }
}

TvPlayerRootAdaptor::TvPlayerRootAdaptor(QObject* parent) : QDBusAbstractAdaptor(parent) {}
