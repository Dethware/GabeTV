#include "BaseLayer.h"

#include <QDBusConnection>
#include <QGuiApplication>
#include <QLoggingCategory>

#include <xcb/xtest.h>

#include <cstdlib>
#include <memory>

Q_LOGGING_CATEGORY(lcBaseLayer, "tvshell.baselayer")

namespace {

// Steam's UI (re)start: it rewrites the base layer, then writes GAMESCOPE_BLUR_MODE (seen at
// +30 ms and +340 ms) without opening any menu. Short, so real menu presses still count.
constexpr int SteamRestartWindowMs = 2000;
constexpr int PowerHoldMs = 800;
constexpr int PowerReleaseSettleMs = 80;

template<typename T>
using XcbReply = std::unique_ptr<T, decltype(&std::free)>;

template<typename T>
XcbReply<T> reply(T *raw)
{
    return XcbReply<T>(raw, &std::free);
}

QString toString(const QList<quint32> &ids)
{
    QStringList parts;
    for (quint32 id : ids)
        parts << QString::number(id);
    return parts.join(u", ");
}

} // namespace

BaseLayer::BaseLayer(QObject *parent)
    : QObject(parent)
    , m_claim(qEnvironmentVariableIntValue("TVSHELL_NO_CLAIM") == 0)
{
    m_powerHoldTimer.setSingleShot(true);
    m_powerHoldTimer.setInterval(PowerHoldMs);
    connect(&m_powerHoldTimer, &QTimer::timeout, this, [this] {
        qCInfo(lcBaseLayer) << "Power held";
        emit powerHeld();
    });
    m_powerReleaseTimer.setSingleShot(true);
    m_powerReleaseTimer.setInterval(PowerReleaseSettleMs);
    connect(&m_powerReleaseTimer, &QTimer::timeout, this, [this] {
        m_powerDown = false;
        if (m_powerHoldTimer.isActive()) {
            m_powerHoldTimer.stop();
            qCInfo(lcBaseLayer) << "Power pressed";
            emit powerPressed();
        }
    });
    connectToX11();
}

BaseLayer::~BaseLayer()
{
    if (m_root) {
        qGuiApp->removeNativeEventFilter(this);
        release();
    }
}

void BaseLayer::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    m_window = window;
    tagWindow();
    evaluate();
    emit windowChanged();
}

void BaseLayer::setClaim(bool claim)
{
    if (m_claim == claim)
        return;
    m_claim = claim;
    if (m_claim)
        evaluate();
    else
        release();
    emit claimChanged();
}

void BaseLayer::setFrontApp(quint32 appId)
{
    if (m_frontApp == appId)
        return;
    m_frontApp = appId;
    emit frontAppChanged();
    evaluate();
}

void BaseLayer::connectToX11()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    // No X11 at all (another platform plugin, or "offscreen" in a test run).
    if (!x11 || !x11->connection())
        return;
    m_connection = x11->connection();
    const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;

    // Only act inside gamescope with Steam integration, never on a normal X11 desktop.
    if (readRootCardinals(atom("GAMESCOPE_PID")).isEmpty()) {
        m_root = 0;
        qCInfo(lcBaseLayer) << "not running in gamescope; not touching the base layer";
        return;
    }
    m_root = root;
    m_steamGame = atom("STEAM_GAME");
    m_baseLayer = atom("GAMESCOPECTRL_BASELAYER_APPID");
    m_focusedApp = atom("GAMESCOPE_FOCUSED_APP");
    // Steam writes this whenever it opens its main menu or Quick Access.
    m_blurMode = atom("GAMESCOPE_BLUR_MODE");
    m_inputCounter = atom("GAMESCOPE_INPUT_COUNTER");

    // Watch the root window's properties. Keep whatever Qt already selected there.
    auto attributes = reply(xcb_get_window_attributes_reply(
        m_connection, xcb_get_window_attributes(m_connection, m_root), nullptr));
    const uint32_t mask = (attributes ? attributes->your_event_mask : 0) | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(m_connection, m_root, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(m_connection);

    qGuiApp->installNativeEventFilter(this);
    grabKeys();
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"), this, SLOT(prepareForSleep(bool)));
    emit availableChanged();
}

xcb_keycode_t BaseLayer::keycodeFor(xcb_keysym_t keysym) const
{
    const xcb_setup_t *setup = xcb_get_setup(m_connection);
    const int count = setup->max_keycode - setup->min_keycode + 1;
    auto mapping = reply(xcb_get_keyboard_mapping_reply(
        m_connection, xcb_get_keyboard_mapping(m_connection, setup->min_keycode, count), nullptr));
    if (!mapping)
        return 0;
    const xcb_keysym_t *keysyms = xcb_get_keyboard_mapping_keysyms(mapping.get());
    const int perKeycode = mapping->keysyms_per_keycode;
    for (int i = 0; i < count * perKeycode; ++i) {
        if (keysyms[i] == keysym)
            return setup->min_keycode + i / perKeycode;
    }
    return 0;
}

void BaseLayer::toggleQuickAccess()
{
    if (!m_root)
        return;
    constexpr xcb_keysym_t ControlL = 0xFFE3;
    constexpr xcb_keysym_t Digit2 = 0x0032;
    const xcb_keycode_t control = keycodeFor(ControlL);
    const xcb_keycode_t two = keycodeFor(Digit2);
    if (!control || !two)
        return;
    qCInfo(lcBaseLayer) << "sending Ctrl+2 (Quick Access)";
    xcb_test_fake_input(m_connection, XCB_KEY_PRESS, control, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_test_fake_input(m_connection, XCB_KEY_PRESS, two, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_test_fake_input(m_connection, XCB_KEY_RELEASE, two, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_test_fake_input(m_connection, XCB_KEY_RELEASE, control, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_flush(m_connection);
}

void BaseLayer::grabKeys()
{
    constexpr xcb_keysym_t XF86HomePage = 0x1008FF18;
    constexpr xcb_keysym_t XF86Sleep = 0x1008FF2F;
    m_homeKeycode = grabKey(XF86HomePage, "Home");
    m_sleepKeycode = grabKey(XF86Sleep, "Sleep");
    constexpr xcb_keysym_t XF86Launch1 = 0x1008FF41;
    m_youtubeKeycode = grabKey(XF86Launch1, "YouTube");
    constexpr xcb_keysym_t XF86Launch2 = 0x1008FF42;
    m_netflixKeycode = grabKey(XF86Launch2, "Netflix");
    constexpr xcb_keysym_t XF86AudioMute = 0x1008FF12;
    m_muteKeycode = grabKey(XF86AudioMute, "Mute");
}

void BaseLayer::prepareForSleep(bool start)
{
    if (start || !m_root)
        return;
    qCInfo(lcBaseLayer) << "resumed: grabbing the global keys again";
    for (xcb_keycode_t keycode :
         {m_homeKeycode, m_sleepKeycode, m_youtubeKeycode, m_netflixKeycode, m_muteKeycode}) {
        if (keycode)
            xcb_ungrab_key(m_connection, keycode, m_root, XCB_MOD_MASK_ANY);
    }
    m_powerDown = false;
    m_powerHoldTimer.stop();
    m_powerReleaseTimer.stop();
    grabKeys();
}

// Passive grab on the root window: the key reaches us whichever window has focus.
xcb_keycode_t BaseLayer::grabKey(xcb_keysym_t keysym, const char *name)
{
    const xcb_keycode_t keycode = keycodeFor(keysym);
    if (!keycode) {
        qCWarning(lcBaseLayer) << "no keycode for" << name;
        return 0;
    }
    // owner_events = false: while the grab is active (key held), the press *and* the release
    // come to the root window, where we look for them. With true, X delivered the release to the
    // shell's own window instead, and Power looked stuck down.
    auto error = reply(xcb_request_check(
        m_connection, xcb_grab_key_checked(m_connection, 0, m_root, XCB_MOD_MASK_ANY, keycode,
                                           XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC)));
    if (error) {
        qCWarning(lcBaseLayer) << "could not grab" << name << "(keycode" << keycode << "), X error" << error->error_code;
        return 0;
    }
    qCInfo(lcBaseLayer) << "grabbed" << name << "keycode" << keycode;
    return keycode;
}

void BaseLayer::tagWindow()
{
    if (!m_root || !m_window)
        return;
    const quint32 id = AppId;
    xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(m_window->winId()),
                        m_steamGame, XCB_ATOM_CARDINAL, 32, 1, &id);
    xcb_flush(m_connection);
}

void BaseLayer::evaluate()
{
    if (!m_root)
        return;

    const QList<quint32> layer = readRootCardinals(m_baseLayer);
    bool game = false;
    for (quint32 id : layer) {
        if (!isOurs(id) && id != SteamUiAppId && id != SteamPlaceholderAppId)
            game = true;
    }
    if (m_gameRunning != game) {
        m_gameRunning = game;
        emit gameRunningChanged();
    }

    // Ours first: the front app if there is one, then the shell; then whatever else is there.
    QList<quint32> ours;
    if (m_frontApp)
        ours << m_frontApp;
    ours << AppId;
    if (m_claim && m_window && !game && layer.mid(0, ours.size()) != ours) {
        QList<quint32> claimed = ours;
        for (quint32 id : layer) {
            if (!isOurs(id))
                claimed << id;
        }
        if (claimed.size() == ours.size())
            claimed << SteamUiAppId;
        qCInfo(lcBaseLayer) << "claiming the screen:" << toString(layer) << "->" << toString(claimed);
        writeBaseLayer(claimed);
    }

    const quint32 focused = readRootCardinals(m_focusedApp).value(0);
    const bool onScreen = focused == AppId;
    if (m_onScreen != onScreen) {
        m_onScreen = onScreen;
        emit onScreenChanged();
    }
    const bool frontOnScreen = m_frontApp != 0 && focused == m_frontApp;
    if (m_frontAppOnScreen != frontOnScreen) {
        m_frontAppOnScreen = frontOnScreen;
        emit frontAppOnScreenChanged();
    }
}

void BaseLayer::release()
{
    if (!m_root)
        return;
    const QList<quint32> layer = readRootCardinals(m_baseLayer);
    if (!layer.contains(AppId))
        return;
    QList<quint32> released;
    for (quint32 id : layer) {
        if (!isOurs(id))
            released << id;
    }
    if (released.isEmpty())
        released << SteamPlaceholderAppId << SteamUiAppId;
    qCInfo(lcBaseLayer) << "releasing the screen:" << toString(layer) << "->" << toString(released);
    writeBaseLayer(released);
}

bool BaseLayer::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "xcb_generic_event_t")
        return false;
    auto *event = static_cast<xcb_generic_event_t *>(message);
    const uint8_t type = event->response_type & ~0x80;
    if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
        auto *key = reinterpret_cast<xcb_key_press_event_t *>(event);
        if (!key->detail)
            return false;
        // A Power release must never get lost, wherever X delivers it.
        if (type == XCB_KEY_RELEASE && key->detail == m_sleepKeycode && key->event != m_root) {
            m_powerReleaseTimer.start();
            return true;
        }
        if (key->event != m_root)
            return false;
        qCDebug(lcBaseLayer) << (type == XCB_KEY_PRESS ? "root key press" : "root key release") << key->detail
                             << "time" << key->time << "state" << key->state;
        if (key->detail == m_homeKeycode) {
            if (type == XCB_KEY_PRESS) {
                qCInfo(lcBaseLayer) << "Home pressed (global grab)";
                emit homePressed();
            }
            return true;
        }
        if (key->detail == m_youtubeKeycode) {
            if (type == XCB_KEY_PRESS) {
                qCInfo(lcBaseLayer) << "YouTube pressed (global grab)";
                emit youtubePressed();
            }
            return true;
        }
        if (key->detail == m_netflixKeycode) {
            if (type == XCB_KEY_PRESS) {
                qCInfo(lcBaseLayer) << "Netflix pressed (global grab)";
                emit netflixPressed();
            }
            return true;
        }
        if (key->detail == m_muteKeycode) {
            if (type == XCB_KEY_PRESS)
                emit mutePressed();
            return true;
        }
        if (key->detail == m_sleepKeycode) {
            if (type == XCB_KEY_PRESS) {
                if (m_powerReleaseTimer.isActive()) {
                    m_powerReleaseTimer.stop(); // auto-repeat: still held
                } else if (!m_powerDown) {
                    m_powerDown = true;
                    m_powerHoldTimer.start();
                }
            } else {
                m_powerReleaseTimer.start();
            }
            return true;
        }
    }
    if (type != XCB_PROPERTY_NOTIFY)
        return false;
    auto *notify = reinterpret_cast<xcb_property_notify_event_t *>(event);
    if (notify->window != m_root)
        return false;
    if (notify->atom == m_baseLayer || notify->atom == m_focusedApp) {
        if (notify->atom == m_baseLayer) {
            // Steam replaced the list (our ID gone) while no game was running: Steam (re)started.
            // A game ending also drops our ID, but then a game was running until now.
            const QList<quint32> layer = readRootCardinals(m_baseLayer);
            if (!m_gameRunning && !layer.contains(AppId) && m_claim)
                m_sinceSteamStart.start();
        }
        QMetaObject::invokeMethod(this, &BaseLayer::evaluate, Qt::QueuedConnection);
    } else if (notify->atom == m_inputCounter) {
        emit inputActivity();
    } else if (notify->atom == m_blurMode && m_sinceSteamStart.isValid()
               && m_sinceSteamStart.elapsed() < SteamRestartWindowMs) {
        qCInfo(lcBaseLayer) << "ignoring Steam's blur write while Steam is starting";
    } else if (notify->atom == m_blurMode && (m_onScreen || m_frontAppOnScreen) && !m_gameRunning) {
        qCInfo(lcBaseLayer) << "Steam opened its menu";
        emit steamMenuOpened();
    }
    return false;
}

xcb_atom_t BaseLayer::atom(const char *name) const
{
    auto atomReply = reply(xcb_intern_atom_reply(
        m_connection, xcb_intern_atom(m_connection, 0, qstrlen(name), name), nullptr));
    return atomReply ? atomReply->atom : XCB_ATOM_NONE;
}

QList<quint32> BaseLayer::readRootCardinals(xcb_atom_t atom) const
{
    QList<quint32> ids;
    const xcb_window_t root = m_root ? m_root : xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;
    auto property = reply(xcb_get_property_reply(
        m_connection, xcb_get_property(m_connection, 0, root, atom, XCB_ATOM_CARDINAL, 0, 64), nullptr));
    if (!property || property->format != 32)
        return ids;
    const auto *values = static_cast<const quint32 *>(xcb_get_property_value(property.get()));
    for (int i = 0; i < xcb_get_property_value_length(property.get()) / 4; ++i)
        ids << values[i];
    return ids;
}

void BaseLayer::writeBaseLayer(const QList<quint32> &ids)
{
    xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_root, m_baseLayer, XCB_ATOM_CARDINAL, 32,
                        ids.size(), ids.constData());
    xcb_flush(m_connection);
}
