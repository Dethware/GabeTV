#pragma once

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <xcb/xcb.h>

// Puts the shell's window on screen inside Steam's gamescope session.
//
// gamescope shows the first app ID in the root property GAMESCOPECTRL_BASELAYER_APPID that has
// a window tagged with that ID (window property STEAM_GAME). Steam rewrites the property when a
// game starts or exits and when Steam itself restarts. While a game is running we leave it
// alone; as soon as only Steam's own entries are left, we put our ID first again.
class BaseLayer : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool claim READ claim WRITE setClaim NOTIFY claimChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)
    Q_PROPERTY(bool onScreen READ onScreen NOTIFY onScreenChanged)

public:
    // Our app ID: below the non-Steam shortcut range (high bit set), far above Steam's app IDs.
    static constexpr quint32 AppId = 0x7E000001;
    // Steam's own entries: its UI (769) and a value it always puts first (413091).
    static constexpr quint32 SteamUiAppId = 769;
    static constexpr quint32 SteamPlaceholderAppId = 413091;

    explicit BaseLayer(QObject *parent = nullptr);
    ~BaseLayer() override;

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);

    // Whether to take the screen. False hands it back to Steam (also set by TVSHELL_NO_CLAIM=1).
    bool claim() const { return m_claim; }
    void setClaim(bool claim);

    // Running inside gamescope with Steam integration.
    bool available() const { return m_root != 0; }
    bool gameRunning() const { return m_gameRunning; }
    // gamescope reports our app ID as the focused one.
    bool onScreen() const { return m_onScreen; }

    // Steam's Big Picture shortcut for Quick Access (Ctrl+2), sent as real input via XTest to
    // whichever window has focus. Toggles: only call it while Steam has the screen.
    Q_INVOKABLE void toggleQuickAccess();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    // The remote's Home key, grabbed on the root window: arrives even while Steam has focus.
    void homePressed();
    // Steam opened its main menu or Quick Access while the shell was on screen. Steam draws those
    // in its own window, which is behind ours: the shell should step aside (claim = false).
    void steamMenuOpened();
    void windowChanged();
    void claimChanged();
    void availableChanged();
    void gameRunningChanged();
    void onScreenChanged();

private:
    void connectToX11();
    void grabHomeKey();
    xcb_keycode_t keycodeFor(xcb_keysym_t keysym) const;
    void tagWindow();
    void evaluate();
    void release();
    xcb_atom_t atom(const char *name) const;
    QList<quint32> readRootCardinals(xcb_atom_t atom) const;
    void writeBaseLayer(const QList<quint32> &ids);

    QPointer<QWindow> m_window;
    bool m_claim = true;
    bool m_gameRunning = false;
    bool m_onScreen = false;

    xcb_connection_t *m_connection = nullptr;
    xcb_window_t m_root = 0;
    xcb_atom_t m_steamGame = 0;
    xcb_atom_t m_baseLayer = 0;
    xcb_atom_t m_focusedApp = 0;
    xcb_atom_t m_blurMode = 0;
    xcb_keycode_t m_homeKeycode = 0;
};
