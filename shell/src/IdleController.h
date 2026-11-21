#pragma once

#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// The shell's idle chain, replacing Steam's: screensaver → screen off → suspend.
// All three count from the last input; suspend only happens once the screen is off.
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
    // Seconds, 0 = never. Setting one saves it to the config file and applies it at once.
    Q_PROPERTY(int screensaverAfter READ screensaverAfter WRITE setScreensaverAfter NOTIFY timesChanged)
    Q_PROPERTY(int screenOffAfter READ screenOffAfter WRITE setScreenOffAfter NOTIFY timesChanged)
    Q_PROPERTY(int suspendAfter READ suspendAfter WRITE setSuspendAfter NOTIFY timesChanged)

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

    int screensaverAfter() const { return m_screensaverAfter; }
    void setScreensaverAfter(int seconds);
    int screenOffAfter() const { return m_screenOffAfter; }
    void setScreenOffAfter(int seconds);
    int suspendAfter() const { return m_suspendAfter; }
    void setSuspendAfter(int seconds);

    // Any user input, from anywhere.
    Q_INVOKABLE void activity();
    // The remote's Power: screen off now, wherever we are.
    Q_INVOKABLE void screenOff();
    // A short Power press: screen off, or on again if it's off. The press itself already counts
    // as input and wakes the screen before the press is complete (on release), so "just woke"
    // means the press was meant to turn the screen on.
    Q_INVOKABLE void togglePower();

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void countingChanged();
    void stateChanged();
    void timesChanged();

private slots:
    // logind: true before suspend, false after resume.
    void prepareForSleep(bool start);

private:
    void tick();
    void setState(State state);
    void setScreenSleeping(bool sleeping);
    void takeSuspendKey();
    void suspend();
    void saveTime(const QString &key, int &member, int seconds);

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
