#include "IdleController.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcIdle, "tvshell.idle")

namespace {

QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/tvshell/tvshell.conf");
}

constexpr int TickMs = 1000;
// Key presses this soon after waking up only woke us; don't let them act.
constexpr int SwallowAfterWakeMs = 400;
// The Power press that turned the screen off also shows up as input (gamescope's input
// counter, its key release): don't let that wake the screen right away.
constexpr int IgnoreAfterScreenOffMs = 1000;
// A short Power press completes (on release) this soon after its press woke the screen.
constexpr int JustWokeMs = 1500;

} // namespace

IdleController::IdleController(QObject *parent)
    : QObject(parent)
{
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("idle"));
    m_screensaverAfter = settings.value(QStringLiteral("screensaverAfter"), m_screensaverAfter).toInt();
    m_screenOffAfter = settings.value(QStringLiteral("screenOffAfter"), m_screenOffAfter).toInt();
    m_suspendAfter = settings.value(QStringLiteral("suspendAfter"), m_suspendAfter).toInt();
    qCInfo(lcIdle) << "screensaver after" << m_screensaverAfter << "s, screen off after" << m_screenOffAfter
                   << "s, suspend after" << m_suspendAfter << "s (0 = never)";

    m_sinceActivity.start();
    m_tick.setInterval(TickMs);
    connect(&m_tick, &QTimer::timeout, this, &IdleController::tick);
    m_tick.start();

    QCoreApplication::instance()->installEventFilter(this);
    takeSuspendKey();
    // The press that wakes the machine usually goes to Steam, not to us.
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"), this, SLOT(prepareForSleep(bool)));
}

void IdleController::prepareForSleep(bool start)
{
    if (!start) {
        qCInfo(lcIdle) << "resumed";
        activity();
    }
}

IdleController::~IdleController()
{
    setScreenSleeping(false);
}

void IdleController::setCounting(bool counting)
{
    if (m_counting == counting)
        return;
    m_counting = counting;
    // Start fresh whenever the shell comes back; never leave its screensaver up behind a game.
    m_sinceActivity.restart();
    if (!counting && m_state == State::Screensaver)
        setState(State::Active);
    emit countingChanged();
}

void IdleController::setScreensaverAfter(int seconds)
{
    saveTime(QStringLiteral("screensaverAfter"), m_screensaverAfter, seconds);
}

void IdleController::setScreenOffAfter(int seconds)
{
    saveTime(QStringLiteral("screenOffAfter"), m_screenOffAfter, seconds);
}

void IdleController::setSuspendAfter(int seconds)
{
    saveTime(QStringLiteral("suspendAfter"), m_suspendAfter, seconds);
}

void IdleController::saveTime(const QString &key, int &member, int seconds)
{
    seconds = qMax(0, seconds);
    if (member == seconds)
        return;
    member = seconds;
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("idle/") + key, seconds);
    qCInfo(lcIdle) << key << "set to" << seconds << "s";
    emit timesChanged();
}

void IdleController::activity()
{
    if (m_state == State::ScreenOff && m_sinceScreenOffRequest.isValid()
        && m_sinceScreenOffRequest.elapsed() < IgnoreAfterScreenOffMs)
        return;
    m_sinceActivity.restart();
    if (m_suspended) {
        m_suspended = false;
        qCInfo(lcIdle) << "back from suspend";
    }
    if (m_state != State::Active) {
        m_sinceWake.start();
        setState(State::Active);
    }
}

void IdleController::togglePower()
{
    const bool justWoke = m_sinceWake.isValid() && m_sinceWake.elapsed() < JustWokeMs;
    if (m_state == State::ScreenOff || justWoke)
        activity();
    else
        screenOff();
}

void IdleController::screenOff()
{
    qCInfo(lcIdle) << "screen off requested";
    m_sinceActivity.restart();
    m_sinceScreenOffRequest.start();
    setState(State::ScreenOff);
}

bool IdleController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const bool waking = m_state != State::Active;
        if (event->type() == QEvent::KeyPress)
            activity();
        if (waking || (m_sinceWake.isValid() && m_sinceWake.elapsed() < SwallowAfterWakeMs))
            return true;
    }
    return QObject::eventFilter(watched, event);
}

void IdleController::tick()
{
    const qint64 idleSeconds = m_sinceActivity.elapsed() / 1000;
    if (m_state == State::ScreenOff) {
        if (m_counting && m_suspendAfter > 0 && !m_suspended && idleSeconds >= m_suspendAfter)
            suspend();
        return;
    }
    if (!m_counting)
        return;
    if (m_screenOffAfter > 0 && idleSeconds >= m_screenOffAfter)
        setState(State::ScreenOff);
    else if (m_state == State::Active && m_screensaverAfter > 0 && idleSeconds >= m_screensaverAfter)
        setState(State::Screensaver);
}

void IdleController::setState(State state)
{
    if (m_state == state)
        return;
    qCInfo(lcIdle) << "state" << m_state << "->" << state;
    if (state == State::ScreenOff)
        setScreenSleeping(true);
    else if (m_state == State::ScreenOff)
        setScreenSleeping(false);
    m_state = state;
    emit stateChanged();
}

void IdleController::setScreenSleeping(bool sleeping)
{
    // gamescopectl finds gamescope through GAMESCOPE_WAYLAND_DISPLAY / XDG_RUNTIME_DIR,
    // both set in the session environment the unit reads.
    if (qEnvironmentVariableIsEmpty("GAMESCOPE_WAYLAND_DISPLAY"))
        return;
    QProcess::execute(QStringLiteral("gamescopectl"),
                      {QStringLiteral("drm_sleep_external_screen"), sleeping ? QStringLiteral("1") : QStringLiteral("0")});
}

void IdleController::takeSuspendKey()
{
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                               QStringLiteral("/org/freedesktop/login1"),
                                               QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("Inhibit"));
    call << QStringLiteral("handle-suspend-key") << QStringLiteral("tvshell")
         << QStringLiteral("The remote's Power button turns the screen off") << QStringLiteral("block");
    const QDBusReply<QDBusUnixFileDescriptor> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid()) {
        qCWarning(lcIdle) << "could not take the suspend key from logind:" << reply.error().message();
        return;
    }
    m_suspendKeyLock = reply.value();
    qCInfo(lcIdle) << "took the suspend key from logind";
}

void IdleController::suspend()
{
    qCInfo(lcIdle) << "idle for" << m_suspendAfter << "s with the screen off: suspending";
    m_suspended = true;
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                               QStringLiteral("/org/freedesktop/login1"),
                                               QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("Suspend"));
    call << false;
    QDBusConnection::systemBus().asyncCall(call);
}
