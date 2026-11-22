#pragma once

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <xcb/xcb.h>

// Puts the shell's window on screen inside Steam's gamescope session.
//
// gamescope shows the first app ID in the root property GAMESCOPECTRL_BASELAYER_APPID that has
// a window tagged with that ID (window property STEAM_GAME). Steam rewrites the property when a
// game starts or exits and when Steam itself restarts. While a game is running we leave it
// alone; as soon as only Steam's own entries are left, we put our ID first again.
//
// A program the shell manages itself (ManagedApp) has an ID of ours too. Setting it as
// frontApp shows that program in place of the shell: its ID goes first, the shell's second.
class BaseLayer : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool claim READ claim WRITE setClaim NOTIFY claimChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)
    Q_PROPERTY(bool onScreen READ onScreen NOTIFY onScreenChanged)
    // The managed app to show in place of the shell (its app ID), 0 for the shell itself.
    Q_PROPERTY(quint32 frontApp READ frontApp WRITE setFrontApp NOTIFY frontAppChanged)
    Q_PROPERTY(bool frontAppOnScreen READ frontAppOnScreen NOTIFY frontAppOnScreenChanged)

public:
    // Our app ID: below the non-Steam shortcut range (high bit set), far above Steam's app IDs.
    static constexpr quint32 AppId = 0x7E000001;
    // The IDs right after it are for programs the shell manages (0x7E000002 is Jellyfin).
    static constexpr bool isOurs(quint32 id) { return (id & 0xFFFFFF00u) == 0x7E000000u; }
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

    quint32 frontApp() const { return m_frontApp; }
    void setFrontApp(quint32 appId);
    // gamescope reports the front app's ID as the focused one.
    bool frontAppOnScreen() const { return m_frontAppOnScreen; }

    // Steam's Big Picture shortcut for Quick Access (Ctrl+2), sent as real input via XTest to
    // whichever window has focus. Toggles: only call it while Steam has the screen.
    Q_INVOKABLE void toggleQuickAccess();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    // The remote's Home key, grabbed on the root window: arrives even while Steam has focus.
    void homePressed();
    // The remote's YouTube key (remapped to XF86Launch1), grabbed the same way.
    void youtubePressed();
    // The remote's Netflix key (remapped to XF86Launch2), grabbed the same way: opens Jellyfin.
    void netflixPressed();
    // The remote's Mute key, grabbed the same way: nothing else acts on it.
    void mutePressed();
    // The remote's Power (remapped to the sleep key), grabbed the same way: a short press...
    void powerPressed();
    // ...or held for a moment (fires while still held).
    void powerHeld();
    // gamescope counted user input (GAMESCOPE_INPUT_COUNTER), wherever it went.
    void inputActivity();
    // Steam opened its main menu or Quick Access while the shell (or its front app) was on
    // screen. Steam draws those in its own window, which is behind ours: the shell should step
    // aside (claim = false).
    void steamMenuOpened();
    void windowChanged();
    void claimChanged();
    void availableChanged();
    void gameRunningChanged();
    void onScreenChanged();
    void frontAppChanged();
    void frontAppOnScreenChanged();

private slots:
    // logind: false after resume. Grab both keys afresh and forget any half-seen Power press
    // (its release may have happened while we were suspended).
    void prepareForSleep(bool start);

private:
    void connectToX11();
    void grabKeys();
    xcb_keycode_t grabKey(xcb_keysym_t keysym, const char *name);
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
    quint32 m_frontApp = 0;
    bool m_frontAppOnScreen = false;

    xcb_connection_t *m_connection = nullptr;
    xcb_window_t m_root = 0;
    xcb_atom_t m_steamGame = 0;
    xcb_atom_t m_baseLayer = 0;
    xcb_atom_t m_focusedApp = 0;
    xcb_atom_t m_blurMode = 0;
    xcb_atom_t m_inputCounter = 0;
    xcb_keycode_t m_homeKeycode = 0;
    xcb_keycode_t m_sleepKeycode = 0;
    xcb_keycode_t m_youtubeKeycode = 0;
    xcb_keycode_t m_netflixKeycode = 0;
    xcb_keycode_t m_muteKeycode = 0;
    // Power short/long press. A held key may auto-repeat as release+press pairs, so a release
    // only counts when no press follows quickly.
    bool m_powerDown = false;
    QTimer m_powerHoldTimer;
    QTimer m_powerReleaseTimer;
    // Since Steam's UI last (re)started. Steam writes GAMESCOPE_BLUR_MODE a few times while
    // starting; that isn't its menu opening.
    QElapsedTimer m_sinceSteamStart;
};
