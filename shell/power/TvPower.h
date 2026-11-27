#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// The TV's power, over HDMI-CEC through SteamOS's cecd (user session D-Bus): the TV is just a
// screen, so it goes to standby when the shell's screen sleeps and comes on when it wakes.
// The soundbar on the TV's ARC follows the TV.
//
// Steam's HDMI CEC settings must not let the TV act on the machine: with "Sleep when turning
// off the TV" on, the TV's answer to our standby puts the machine to sleep. And "Turn on TV
// when resuming from sleep" should be off, since the shell keeps the screen off after a wake
// by the remote (IdleController's ready state). cecd's "Turn off TV when going to sleep" may
// stay on: after a sleep that left the screen on (one Steam started), the TV is woken again
// here.
class TvPower : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // false: the TV is told to go to standby; true: to come on and show this machine.
    Q_PROPERTY(bool on READ on WRITE setOn NOTIFY onChanged)

public:
    explicit TvPower(QObject *parent = nullptr);

    bool on() const { return m_on; }
    void setOn(bool on);

signals:
    void onChanged();

private slots:
    // logind: true before a sleep, false after the resume.
    void prepareForSleep(bool start);

private:
    void call(const QString &method, const QVariantList &arguments);

    bool m_on = true;
};
