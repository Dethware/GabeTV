#pragma once

#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// The shell's idle chain, replacing Steam's: screensaver → screen off → suspend.
// All three count from the last input; suspend only happens once the screen is off.
//
// Three kinds of activity hold the chain back, each differently:
//   input       activity(), key presses: restarts the idle time.
//   foreground  `watching`: something on screen is being watched (a video). No screensaver,
//               no screen off, no suspend while it lasts; when it ends, the idle time counts
//               from that moment, not from the last input long before.
//   background  `audioPlaying`: music with nothing to watch. See below.
// The time since the last input is kept apart from the foreground on purpose: a later "sleep
// after an hour without input, whatever plays" only needs that clock.
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
// Sleep and the remote (../docs/gabecube-findings.md, "Two ways of going to sleep"): the
// remote can wake the machine only if it let go of its connection by itself before the
// machine went to sleep. So the machine is put to sleep only while `remoteConnected` is
// false, which happens about 30 minutes after the remote was last used; until then it waits
// with the screen off. Two ways to get there:
//   idle         screen off, then suspendAfter seconds without input;
//   sleep()      asked for (the remote's Power, the power dialog's Sleep): screen off now,
//                asleep as soon as the remote lets go.
//
// The ready state: after a sleep the remote didn't hold, a wake where the remote connects
// within ReadyWindowMs is the remote's (picked up, or moved by someone walking by; both look
// the same until a key arrives). The screen stays off, as after sleep(): a key or button from
// anything wakes it fully, otherwise the machine sleeps again once the remote lets go. A wake
// where the remote doesn't connect in time is from something else and wakes fully at once.
//
// It also takes the sleep key (the remote's Power) over from logind with a
// "handle-suspend-key" inhibitor lock, so Power can mean "screen off, then sleep" instead of
// sleeping at once (with the remote still connected, which then couldn't wake it).
class IdleController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool counting READ counting WRITE setCounting NOTIFY countingChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    // Something on screen is being watched (a playing video): foreground activity.
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)
    // Sound that should go on while nobody touches anything (music in the background).
    Q_PROPERTY(bool audioPlaying READ audioPlaying WRITE setAudioPlaying NOTIFY audioPlayingChanged)
    // Seconds, 0 = never. Setting one saves it to the config file and applies it at once.
    Q_PROPERTY(int screensaverAfter READ screensaverAfter WRITE setScreensaverAfter NOTIFY timesChanged)
    Q_PROPERTY(int screenOffAfter READ screenOffAfter WRITE setScreenOffAfter NOTIFY timesChanged)
    Q_PROPERTY(int suspendAfter READ suspendAfter WRITE setSuspendAfter NOTIFY timesChanged)
    // The remote holds its connection (RemoteLink, bound in Main.qml).
    Q_PROPERTY(bool remoteConnected READ remoteConnected WRITE setRemoteConnected NOTIFY remoteConnectedChanged)

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

    bool watching() const { return m_watching; }
    void setWatching(bool watching);

    bool audioPlaying() const { return m_audioPlaying; }
    void setAudioPlaying(bool playing);

    int screensaverAfter() const { return m_screensaverAfter; }
    void setScreensaverAfter(int seconds);
    int screenOffAfter() const { return m_screenOffAfter; }
    void setScreenOffAfter(int seconds);
    int suspendAfter() const { return m_suspendAfter; }
    void setSuspendAfter(int seconds);

    bool remoteConnected() const { return m_remoteConnected; }
    void setRemoteConnected(bool connected);

    // Any user input, from anywhere: restarts the idle time and turns the screen back on, but
    // doesn't end the screensaver (key presses decide that, in eventFilter).
    Q_INVOKABLE void activity();
    // Back to Active from anything: explicit wake-ups (Home, Assistant, the power dialog).
    Q_INVOKABLE void wake();
    // The remote's Power: screen off now, wherever we are.
    Q_INVOKABLE void screenOff();
    // A short Power press: sleep() (screen off now, asleep once the remote lets go), or the
    // screen on again if it's off. The press itself already counts
    // as input and wakes the screen before the press is complete (on release), so "just woke"
    // means the press was meant to turn the screen on.
    Q_INVOKABLE void togglePower();
    // Sleep, asked for: screen off now, asleep as soon as the remote lets go (at once if it
    // isn't connected). Any input before that wakes the screen and calls it off.
    Q_INVOKABLE void sleep();

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void countingChanged();
    void stateChanged();
    void timesChanged();
    void audioPlayingChanged();
    void watchingChanged();
    void remoteConnectedChanged();
    // The machine is about to suspend (whoever asked for it).
    void aboutToSleep();

private slots:
    // logind: true before suspend, false after resume.
    void prepareForSleep(bool start);

private:
    void tick();
    void setState(State state);
    void setScreenSleeping(bool sleeping);
    void sendScreenSleeping();
    void repeatScreenWake();
    void applyAudioPlaying(bool playing);
    void takeSuspendKey();
    // Sleeps now if sleep is due (asked for, or the idle time is up) and nothing holds it
    // back: music, something watched, the remote still connected. Called every tick and when
    // the remote lets go.
    void sleepIfDue();
    void suspend(const QString &reason);
    // Since the last input, or since something stopped being watched if that was later.
    qint64 idleSeconds() const;
    // The remote woke us: screen off until a key, asleep again once it lets go.
    void enterReady();
    // The ready window after a resume ran out without the remote: something else woke us.
    void readyWindowEnded();
    void saveTime(const QString &key, int &member, int seconds);

    bool m_counting = false;
    bool m_audioPlaying = false;
    bool m_watching = false;
    bool m_screenSleeping = false;
    State m_state = State::Active;
    // Since the last input.
    QElapsedTimer m_sinceActivity;
    // Since something stopped being watched (invalid until that first happens).
    QElapsedTimer m_sinceWatched;
    QElapsedTimer m_sinceWake;
    QElapsedTimer m_sinceScreenOffRequest;
    QTimer m_tick;
    // Runs out when the music has really stopped (not just between two tracks).
    QTimer m_audioStopped;
    // Repeats "screen on" a few times after waking: gamescope can miss the first one when
    // it arrives right after a resume, and the screen then stays dark.
    QTimer m_screenWake;
    int m_screenWakeRepeats = 0;
    bool m_suspended = false;
    bool m_remoteConnected = false;
    // Sleep was asked for (sleep(), or a wake by the remote alone): asleep as soon as the
    // remote lets go, whatever the idle time. Input clears it.
    bool m_sleepAsked = false;
    // The remote was connected at the start of the last sleep (so it can't have woken us).
    bool m_remoteHeldSleep = false;
    // "Waiting for the remote" has been logged for this sleep.
    bool m_waitingForRemote = false;
    // Runs after a resume the remote didn't hold: the remote connecting before it ends makes
    // this the remote's wake.
    QTimer m_readyWindow;

    int m_screensaverAfter = 300;
    int m_screenOffAfter = 1800;
    int m_suspendAfter = 3600;

    QDBusUnixFileDescriptor m_suspendKeyLock;
};
