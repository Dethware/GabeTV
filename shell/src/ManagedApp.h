#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <xcb/xcb.h>

// A program the shell starts itself and shows by handing it the screen (Jellyfin Desktop).
//
// The program runs as its own systemd user service, so it survives a restart of the shell;
// this class starts and stops that service, and picks a running one up again when the shell
// starts. Once the program's window exists it is tagged with the app's gamescope ID
// (STEAM_GAME), after which BaseLayer can put it on screen (BaseLayer::frontApp).
//
// What the program plays is followed over MPRIS, which is also how it is paused.
class ManagedApp : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The systemd user unit, e.g. "jellyfin-desktop.service".
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY unitChanged)
    // The class of its main window (WM_CLASS), e.g. "jellyfin-desktop": how the window is found.
    Q_PROPERTY(QString windowClass READ windowClass WRITE setWindowClass NOTIFY windowClassChanged)
    // The gamescope app ID its window gets: one of BaseLayer's own IDs (BaseLayer::isOurs).
    Q_PROPERTY(quint32 appId READ appId WRITE setAppId NOTIFY appIdChanged)
    // The start of its MPRIS bus name, e.g. "org.mpris.MediaPlayer2.JellyfinDesktop" (the
    // program appends its profile).
    Q_PROPERTY(QString mprisName READ mprisName WRITE setMprisName NOTIFY mprisNameChanged)

    // The service is active (or starting).
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // Its window exists and carries the app ID: it can be shown.
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // It is playing something right now (MPRIS PlaybackStatus "Playing").
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)

public:
    explicit ManagedApp(QObject *parent = nullptr);

    QString unit() const { return m_unit; }
    void setUnit(const QString &unit);
    QString windowClass() const { return m_windowClass; }
    void setWindowClass(const QString &windowClass);
    quint32 appId() const { return m_appId; }
    void setAppId(quint32 appId);
    QString mprisName() const { return m_mprisName; }
    void setMprisName(const QString &name);

    bool running() const { return m_running; }
    bool ready() const { return m_window != 0; }
    bool playing() const { return m_playing; }

    // Start the service unless it runs already. `ready` follows when its window appears.
    Q_INVOKABLE void start();
    // Stop the service.
    Q_INVOKABLE void quit();
    // Pause what it plays; nothing happens if it plays nothing.
    Q_INVOKABLE void pause();

signals:
    void unitChanged();
    void windowClassChanged();
    void appIdChanged();
    void mprisNameChanged();
    void runningChanged();
    void readyChanged();
    void playingChanged();
    // The program ended on its own (quit from its menu, or crashed) while it was running.
    void exited();

private slots:
    // systemd: something about the unit changed; read its state again.
    void unitPropertiesChanged();
    // The bus: a name appeared or went away (the program's MPRIS service).
    void nameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    // MPRIS: the player's properties changed.
    void playerPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    // systemd (session bus, org.freedesktop.systemd1): load the unit, follow its ActiveState.
    void watchUnit();
    void readUnitState();
    void setRunning(bool running);
    // X11: look for the program's main window and tag it; a known window is checked for still
    // being there. Polled while the service runs.
    void checkWindow();
    xcb_window_t findWindow() const;
    void setWindow(xcb_window_t window);
    // MPRIS (session bus): find the name with our prefix, follow PlaybackStatus.
    void findPlayer();
    void setPlayer(const QString &busName);
    void setPlaying(bool playing);

    QString m_unit;
    QString m_windowClass;
    QString m_mprisName;
    quint32 m_appId = 0;

    bool m_running = false;
    bool m_playing = false;
    // quit() was called: the service stopping is not "exited on its own".
    bool m_stopping = false;
    QDBusObjectPath m_unitPath;
    xcb_connection_t *m_connection = nullptr;
    xcb_window_t m_root = 0;
    xcb_window_t m_window = 0;
    // The full bus name of the program's MPRIS service, once seen.
    QString m_player;
    QTimer m_windowPoll;
};
