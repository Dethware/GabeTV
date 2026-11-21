#include "GamepadInput.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLoggingCategory>

#include <SDL3/SDL.h>

#include <cstdlib>

Q_LOGGING_CATEGORY(lcGamepad, "tvshell.gamepad")

namespace {

constexpr int PollIntervalMs = 8;
constexpr int RepeatDelayMs = 400;
constexpr int RepeatIntervalMs = 120;
// Stick deflection (of 32767) that counts as a direction; lower to let go, to avoid chatter.
constexpr int StickPress = 20000;
constexpr int StickRelease = 12000;

int keyForButton(int button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return Qt::Key_Return;
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_BACK:
        return Qt::Key_Back;
    case SDL_GAMEPAD_BUTTON_START:
        return Qt::Key_Menu;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return Qt::Key_Up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return Qt::Key_Down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return Qt::Key_Left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return Qt::Key_Right;
    default:
        return 0;
    }
}

bool isDirection(int key)
{
    return key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right;
}

} // namespace

GamepadInput::GamepadInput(QObject *parent)
    : QObject(parent)
{
    // Steam owns the real controller (its hidraw devices); only use the evdev gamepads.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    // SDL skips Steam's virtual pad unless told otherwise. SDL reads this switch from the
    // environment only; SDL_SetHint() has no effect on it (checked with SDL 3.4.12).
    qputenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        qCWarning(lcGamepad) << "SDL_Init failed:" << SDL_GetError();
        return;
    }
    m_initialized = true;

    int joystickCount = 0;
    if (SDL_JoystickID *joysticks = SDL_GetJoysticks(&joystickCount)) {
        for (int i = 0; i < joystickCount; ++i) {
            qCInfo(lcGamepad).nospace() << "joystick " << SDL_GetJoystickNameForID(joysticks[i]) << " "
                                        << Qt::hex << SDL_GetJoystickVendorForID(joysticks[i]) << ":"
                                        << SDL_GetJoystickProductForID(joysticks[i])
                                        << (SDL_IsGamepad(joysticks[i]) ? " (gamepad)" : " (not a gamepad)");
        }
        SDL_free(joysticks);
    }
    qCInfo(lcGamepad) << joystickCount << "joysticks at startup";

    m_pollTimer.setInterval(PollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &GamepadInput::poll);
    m_pollTimer.start();

    m_repeatTimer.setSingleShot(false);
    connect(&m_repeatTimer, &QTimer::timeout, this, [this] {
        m_repeatTimer.setInterval(RepeatIntervalMs);
        if (m_heldKey)
            sendKey(m_heldKey, true, true);
    });
}

GamepadInput::~GamepadInput()
{
    if (m_initialized)
        SDL_Quit();
}

void GamepadInput::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    m_window = window;
    emit windowChanged();
}

void GamepadInput::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    if (!enabled && m_heldKey)
        release(m_heldKey);
    m_enabled = enabled;
    emit enabledChanged();
}

void GamepadInput::poll()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            if (SDL_Gamepad *pad = SDL_OpenGamepad(event.gdevice.which)) {
                qCInfo(lcGamepad) << "gamepad added:" << SDL_GetGamepadName(pad);
                ++m_count;
                emit countChanged();
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (SDL_Gamepad *pad = SDL_GetGamepadFromID(event.gdevice.which)) {
                SDL_CloseGamepad(pad);
                --m_count;
                emit countChanged();
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            qCDebug(lcGamepad) << "button down" << SDL_GetGamepadStringForButton(SDL_GamepadButton(event.gbutton.button))
                               << (m_enabled ? "" : "(ignored, not on screen)");
            if (const int key = keyForButton(event.gbutton.button))
                press(key);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (const int key = keyForButton(event.gbutton.button))
                release(key);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
                m_stickX = event.gaxis.value;
            else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
                m_stickY = event.gaxis.value;
            else
                break;
            int key = 0;
            const int threshold = m_stickKey ? StickRelease : StickPress;
            if (std::abs(m_stickX) >= std::abs(m_stickY) && std::abs(m_stickX) >= threshold)
                key = m_stickX < 0 ? Qt::Key_Left : Qt::Key_Right;
            else if (std::abs(m_stickY) > std::abs(m_stickX) && std::abs(m_stickY) >= threshold)
                key = m_stickY < 0 ? Qt::Key_Up : Qt::Key_Down;
            if (key != m_stickKey) {
                if (m_stickKey)
                    release(m_stickKey);
                m_stickKey = key;
                if (key)
                    press(key);
            }
            break;
        }
        default:
            break;
        }
    }
}

void GamepadInput::press(int key)
{
    if (!m_enabled)
        return;
    sendKey(key, true);
    if (isDirection(key)) {
        m_heldKey = key;
        m_repeatTimer.start(RepeatDelayMs);
    }
}

void GamepadInput::release(int key)
{
    if (key == m_heldKey) {
        m_heldKey = 0;
        m_repeatTimer.stop();
    }
    if (m_enabled)
        sendKey(key, false);
}

void GamepadInput::sendKey(int key, bool pressed, bool autoRepeat)
{
    if (!m_window)
        return;
    QKeyEvent event(pressed ? QEvent::KeyPress : QEvent::KeyRelease, key, Qt::NoModifier, QString(), autoRepeat);
    QGuiApplication::sendEvent(m_window, &event);
}
