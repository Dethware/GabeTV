// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QTimer>

// Lets the TV shell pause and continue the game, and see which of the two it is doing,
// through the interface it already uses for its other apps: MPRIS, as far as the shell
// uses it (PlaybackStatus with its change signal, Pause, Play). A game the shell has
// paused is "parked": it stays as it is until the shell continues it.
class TvPlayer : public QObject {
    Q_OBJECT

public:
    enum class State { Stopped, Paused, Playing };

    // What the main window gives: how the emulation is doing, and its own pause and
    // continue. `changed` is called with every new state.
    struct Hooks {
        std::function<State()> state;
        std::function<void()> pause;
        std::function<void()> play;
        std::function<void(State)> changed;
    };

    TvPlayer(Hooks hooks_, QObject* parent);

    QString PlaybackStatus() const;
    void Pause();
    void Play();

private:
    void Check();

    Hooks hooks;
    State state{State::Stopped};
    // The main window has no one signal for "running, paused or stopped": it is asked.
    QTimer timer;
};

class TvPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ PlaybackStatus)
    Q_PROPERTY(bool CanPause READ Yes)
    Q_PROPERTY(bool CanPlay READ Yes)
    Q_PROPERTY(bool CanControl READ Yes)

public:
    explicit TvPlayerAdaptor(TvPlayer* player_);

    QString PlaybackStatus() const;
    bool Yes() const {
        return true;
    }

public slots:
    void Pause();
    void Play();
    void PlayPause();

private:
    TvPlayer* player;
};

class TvPlayerRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ Identity)
    Q_PROPERTY(bool CanQuit READ No)
    Q_PROPERTY(bool CanRaise READ No)

public:
    explicit TvPlayerRootAdaptor(QObject* parent);

    QString Identity() const {
        return QStringLiteral("Eden");
    }
    bool No() const {
        return false;
    }
};
