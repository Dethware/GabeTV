#pragma once

#include <QObject>
#include <QString>

// A controller that exists only in software, made through the kernel's uinput interface:
// what Steam, the emulator and every other program get instead of the Joy-Cons. One per
// unit. It presents itself as an Xbox 360 pad (same id, buttons and axes as the kernel's
// xpad driver gives one), because every program knows that layout without being told.
// Its four letter buttons are named by the letter programs see, not by where an Xbox pad
// has them: A is what confirms, B what goes back. The pad declares a guide button, since
// leaving it out would shift the numbering of the stick buttons, but never presses it:
// Home stays with the shell.
class VirtualPad : public QObject
{
    Q_OBJECT

public:
    enum Button : quint32 {
        A = 1u << 0,
        B = 1u << 1,
        X = 1u << 2,
        Y = 1u << 3,
        LeftShoulder = 1u << 4,
        RightShoulder = 1u << 5,
        Select = 1u << 6,
        Start = 1u << 7,
        LeftStick = 1u << 8,
        RightStick = 1u << 9,
        DpadUp = 1u << 10,
        DpadDown = 1u << 11,
        DpadLeft = 1u << 12,
        DpadRight = 1u << 13,
        LeftTrigger = 1u << 14,
        RightTrigger = 1u << 15,
    };

    struct State
    {
        quint32 buttons = 0; // Button bits
        // -1 to 1, right and down positive.
        double leftX = 0;
        double leftY = 0;
        double rightX = 0;
        double rightY = 0;
    };

    explicit VirtualPad(const QString &name, QObject *parent = nullptr);
    ~VirtualPad() override;

    // False when uinput can't be used.
    bool create();
    void update(const State &state);

private:
    void emitEvent(quint16 type, quint16 code, qint32 value);

    QString m_name;
    int m_fd = -1;
    State m_state;
};
