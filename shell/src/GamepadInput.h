#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

// Controller input through SDL3's gamepad API. In Gaming Mode this reads Steam's virtual
// Xbox pad (Steam Input keeps the real controller). Buttons become key events for the window,
// so QML handles the remote, a keyboard and the controller the same way.
//
// The virtual pad also feeds games: keep `enabled` false whenever the shell isn't on screen.
// While Steam's own UI has focus, Steam reads the controller itself and the pad stays silent.
class GamepadInput : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit GamepadInput(QObject *parent = nullptr);
    ~GamepadInput() override;

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // Connected gamepads.
    int count() const { return m_count; }

signals:
    // Any button or a real stick movement, also while disabled (for idle tracking).
    void activity();
    void windowChanged();
    void enabledChanged();
    void countChanged();

private:
    void poll();
    void press(int key);
    void release(int key);
    void sendKey(int key, bool pressed, bool autoRepeat = false);

    QPointer<QWindow> m_window;
    bool m_enabled = true;
    bool m_initialized = false;
    int m_count = 0;
    QTimer m_pollTimer;

    // Held direction (stick or d-pad) and its auto-repeat.
    int m_heldKey = 0;
    QTimer m_repeatTimer;
    // Left stick position and the direction it currently counts as (0 = centred).
    int m_stickX = 0;
    int m_stickY = 0;
    int m_stickKey = 0;
};
