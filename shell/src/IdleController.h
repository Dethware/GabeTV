#pragma once

#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// The shell's idle chain, replacing Steam's: screensaver → screen off → suspend.
// Idle time only counts while `counting` is true (the shell is on screen, no game).
// Times come from ~/.config/tvshell/tvshell.conf, [idle] group, in seconds (0 = never):
//   screensaverAfter=300, screenOffAfter=1800, suspendAfter=3600
//
// Screen off stops the HDMI signal through gamescope (drm_sleep_external_screen); the machine
// stays awake, so any input turns it back on. The first input after the screensaver or screen
// off only wakes: key presses arriving right then are swallowed.
//
// It also takes the sleep key (the remote's Power) over from logind with a
// "handle-suspend-key" inhibitor lock, so Power can mean "screen off" instead of suspend.
class IdleController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool counting READ counting WRITE setCounting NOTIFY countingChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)

public:
    enum class State {
        Active,
        Screensaver,
        ScreenOff,
    };
    Q_ENUM(State)

    explicit IdleController(QObject *parent = nullptr);
    ~IdleController() override;

    bool counting() const { return m_counting; }
    void setCounting(bool counting);

    State state() const { return m_state; }

    // Any user input, from anywhere.
    Q_INVOKABLE void activity();
    // The remote's Power: screen off now, wherever we are.
    Q_INVOKABLE void screenOff();

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void countingChanged();
    void stateChanged();

private slots:
    // logind: true before suspend, false after resume.
    void prepareForSleep(bool start);

private:
    void tick();
    void setState(State state);
    void setScreenSleeping(bool sleeping);
    void takeSuspendKey();
    void suspend();

    bool m_counting = false;
    State m_state = State::Active;
    QElapsedTimer m_sinceActivity;
    QElapsedTimer m_sinceWake;
    QElapsedTimer m_sinceScreenOffRequest;
    QTimer m_tick;
    bool m_suspended = false;

    int m_screensaverAfter = 300;
    int m_screenOffAfter = 1800;
    int m_suspendAfter = 3600;

    QDBusUnixFileDescriptor m_suspendKeyLock;
};
