#include "BaseLayer.h"

#include <QGuiApplication>
#include <QLoggingCategory>

#include <cstdlib>
#include <memory>

Q_LOGGING_CATEGORY(lcBaseLayer, "tvshell.baselayer")

namespace {

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

void BaseLayer::connectToX11()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11)
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

    // Watch the root window's properties. Keep whatever Qt already selected there.
    auto attributes = reply(xcb_get_window_attributes_reply(
        m_connection, xcb_get_window_attributes(m_connection, m_root), nullptr));
    const uint32_t mask = (attributes ? attributes->your_event_mask : 0) | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(m_connection, m_root, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(m_connection);

    qGuiApp->installNativeEventFilter(this);
    emit availableChanged();
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
        if (id != AppId && id != SteamUiAppId && id != SteamPlaceholderAppId)
            game = true;
    }
    if (m_gameRunning != game) {
        m_gameRunning = game;
        emit gameRunningChanged();
    }

    if (m_claim && m_window && !game && layer.value(0) != AppId) {
        QList<quint32> claimed{AppId};
        for (quint32 id : layer) {
            if (id != AppId)
                claimed << id;
        }
        if (claimed.size() == 1)
            claimed << SteamUiAppId;
        qCInfo(lcBaseLayer) << "claiming the screen:" << toString(layer) << "->" << toString(claimed);
        writeBaseLayer(claimed);
    }

    const bool onScreen = readRootCardinals(m_focusedApp).value(0) == AppId;
    if (m_onScreen != onScreen) {
        m_onScreen = onScreen;
        emit onScreenChanged();
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
        if (id != AppId)
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
    if ((event->response_type & ~0x80) != XCB_PROPERTY_NOTIFY)
        return false;
    auto *notify = reinterpret_cast<xcb_property_notify_event_t *>(event);
    if (notify->window == m_root && (notify->atom == m_baseLayer || notify->atom == m_focusedApp))
        QMetaObject::invokeMethod(this, &BaseLayer::evaluate, Qt::QueuedConnection);
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
