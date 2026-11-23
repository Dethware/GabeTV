#include "VirtualPad.h"

#include <QLoggingCategory>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcPad, "tvcontrollers.pad")

namespace {

struct Key
{
    VirtualPad::Button button;
    quint16 code;
};

// xpad's codes. Its BTN_X is the west button and BTN_Y the north one, whatever the
// kernel's BTN_NORTH and BTN_WEST aliases say.
constexpr Key Keys[] = {
    {VirtualPad::South, BTN_A},
    {VirtualPad::East, BTN_B},
    {VirtualPad::West, BTN_X},
    {VirtualPad::North, BTN_Y},
    {VirtualPad::LeftShoulder, BTN_TL},
    {VirtualPad::RightShoulder, BTN_TR},
    {VirtualPad::Select, BTN_SELECT},
    {VirtualPad::Start, BTN_START},
    {VirtualPad::LeftStick, BTN_THUMBL},
    {VirtualPad::RightStick, BTN_THUMBR},
};

constexpr int StickRange = 32767;
constexpr int TriggerRange = 255;

int stickValue(double position)
{
    return qRound(qBound(-1.0, position, 1.0) * StickRange);
}

int hat(quint32 buttons, quint32 negative, quint32 positive)
{
    return (buttons & positive ? 1 : 0) - (buttons & negative ? 1 : 0);
}

} // namespace

VirtualPad::VirtualPad(const QString &name, QObject *parent)
    : QObject(parent)
    , m_name(name)
{
}

VirtualPad::~VirtualPad()
{
    if (m_fd >= 0) {
        ::ioctl(m_fd, UI_DEV_DESTROY);
        ::close(m_fd);
    }
}

bool VirtualPad::create()
{
    m_fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        qCWarning(lcPad) << "cannot open /dev/uinput:" << std::strerror(errno);
        return false;
    }

    bool ok = ::ioctl(m_fd, UI_SET_EVBIT, EV_KEY) == 0 && ::ioctl(m_fd, UI_SET_EVBIT, EV_ABS) == 0;
    for (const Key &key : Keys)
        ok = ok && ::ioctl(m_fd, UI_SET_KEYBIT, key.code) == 0;
    // Declared and never pressed; see the header.
    ok = ok && ::ioctl(m_fd, UI_SET_KEYBIT, BTN_MODE) == 0;

    const auto axis = [&](quint16 code, int minimum, int maximum, int fuzz, int flat) {
        uinput_abs_setup setup{};
        setup.code = code;
        setup.absinfo.minimum = minimum;
        setup.absinfo.maximum = maximum;
        setup.absinfo.fuzz = fuzz;
        setup.absinfo.flat = flat;
        ok = ok && ::ioctl(m_fd, UI_SET_ABSBIT, code) == 0 && ::ioctl(m_fd, UI_ABS_SETUP, &setup) == 0;
    };
    for (const quint16 code : {ABS_X, ABS_Y, ABS_RX, ABS_RY})
        axis(code, -StickRange - 1, StickRange, 16, 128);
    for (const quint16 code : {ABS_Z, ABS_RZ})
        axis(code, 0, TriggerRange, 0, 0);
    for (const quint16 code : {ABS_HAT0X, ABS_HAT0Y})
        axis(code, -1, 1, 0, 0);

    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x045e;
    setup.id.product = 0x028e;
    setup.id.version = 0x0110;
    std::strncpy(setup.name, m_name.toUtf8().constData(), sizeof setup.name - 1);
    ok = ok && ::ioctl(m_fd, UI_DEV_SETUP, &setup) == 0 && ::ioctl(m_fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        qCWarning(lcPad) << "cannot create the virtual controller:" << std::strerror(errno);
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    qCInfo(lcPad).noquote() << "virtual controller created:" << m_name;
    return true;
}

void VirtualPad::update(const State &state)
{
    if (m_fd < 0)
        return;
    bool changed = false;
    const auto set = [&](quint16 type, quint16 code, int before, int now) {
        if (before == now)
            return;
        emitEvent(type, code, now);
        changed = true;
    };

    for (const Key &key : Keys)
        set(EV_KEY, key.code, bool(m_state.buttons & key.button), bool(state.buttons & key.button));
    set(EV_ABS, ABS_X, stickValue(m_state.leftX), stickValue(state.leftX));
    set(EV_ABS, ABS_Y, stickValue(m_state.leftY), stickValue(state.leftY));
    set(EV_ABS, ABS_RX, stickValue(m_state.rightX), stickValue(state.rightX));
    set(EV_ABS, ABS_RY, stickValue(m_state.rightY), stickValue(state.rightY));
    set(EV_ABS, ABS_Z, m_state.buttons & LeftTrigger ? TriggerRange : 0, state.buttons & LeftTrigger ? TriggerRange : 0);
    set(EV_ABS, ABS_RZ, m_state.buttons & RightTrigger ? TriggerRange : 0,
        state.buttons & RightTrigger ? TriggerRange : 0);
    set(EV_ABS, ABS_HAT0X, hat(m_state.buttons, DpadLeft, DpadRight), hat(state.buttons, DpadLeft, DpadRight));
    set(EV_ABS, ABS_HAT0Y, hat(m_state.buttons, DpadUp, DpadDown), hat(state.buttons, DpadUp, DpadDown));

    m_state = state;
    if (changed)
        emitEvent(EV_SYN, SYN_REPORT, 0);
}

void VirtualPad::emitEvent(quint16 type, quint16 code, qint32 value)
{
    input_event event{};
    event.type = type;
    event.code = code;
    event.value = value;
    if (::write(m_fd, &event, sizeof event) < 0)
        qCWarning(lcPad) << "writing to the virtual controller failed:" << std::strerror(errno);
}
