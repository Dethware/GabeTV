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
// While music plays (audioPlaying) the chain still runs, but "screen off" keeps the signal
// (the sound travels with it) and the shell shows black instead, and the machine doesn't
// suspend. When the music stops, the signal goes off and the time to suspend starts.
//
// It also takes the sleep key (the remote's Power) over from logind with a
// "handle-suspend-key" inhibitor lock, so Power can mean "screen off" instead of suspend.
class IdleController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool counting READ counting WRITE setCounting NOTIFY countingChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    // Sound that should go on while nobody touches anything (music in the background).
    Q_PROPERTY(bool audioPlaying READ audioPlaying WRITE setAudioPlaying NOTIFY audioPlayingChanged)
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

    bool audioPlaying() const { return m_audioPlaying; }
    void setAudioPlaying(bool playing);

    int screensaverAfter() const { return m_screensaverAfter; }
    void setScreensaverAfter(int seconds);
    int screenOffAfter() const { return m_screenOffAfter; }
    void setScreenOffAfter(int seconds);
    int suspendAfter() const { return m_suspendAfter; }
    void setSuspendAfter(int seconds);

    // Any user input, from anywhere: restarts the idle time and turns the screen back on, but
    // doesn't end the screensaver (key presses decide that, in eventFilter).
    Q_INVOKABLE void activity();
    // Back to Active from anything: explicit wake-ups (Home, Assistant, the power dialog).
    Q_INVOKABLE void wake();
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
    void audioPlayingChanged();
    // The machine is about to suspend (whoever asked for it).
    void aboutToSleep();

private slots:
    // logind: true before suspend, false after resume.
    void prepareForSleep(bool start);

private:
    void tick();
    void setState(State state);
    void setScreenSleeping(bool sleeping);
    void applyAudioPlaying(bool playing);
    void takeSuspendKey();
    void suspend();
    void saveTime(const QString &key, int &member, int seconds);

    bool m_counting = false;
    bool m_audioPlaying = false;
    bool m_screenSleeping = false;
    State m_state = State::Active;
    QElapsedTimer m_sinceActivity;
    QElapsedTimer m_sinceWake;
    QElapsedTimer m_sinceScreenOffRequest;
    QTimer m_tick;
    // Runs out when the music has really stopped (not just between two tracks).
    QTimer m_audioStopped;
    bool m_suspended = false;

    int m_screensaverAfter = 300;
    int m_screenOffAfter = 1800;
    int m_suspendAfter = 3600;

    QDBusUnixFileDescriptor m_suspendKeyLock;
};
