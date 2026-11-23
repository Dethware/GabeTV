#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// The shell's view of the controller service (../controllers, `local.tvcontrollers` on the
// session bus): which controllers there are, and the few things the controllers screen
// asks of it. The service owns the Joy-Cons and the Pro Controller; the shell only shows
// and requests.
//
// units:   [{ player, kind: "pair" | "sideways" | "pro", joyCons: [joyCon, …] }]
// waiting: [joyCon, …]   connected, but not part of a controller yet
// joyCon:  { address, name, side, battery (0 to 4), charging, ring (a Ring-Con is attached
//            and being read), lostReports, motionCalibrated }
class ControllerClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // The service answered the last time it was asked.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool searching READ searching NOTIFY changed)
    Q_PROPERTY(QVariantList units READ units NOTIFY changed)
    Q_PROPERTY(QVariantList waiting READ waiting NOTIFY changed)

public:
    explicit ControllerClient(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool searching() const { return m_searching; }
    QVariantList units() const { return m_units; }
    QVariantList waiting() const { return m_waiting; }

    // Searching for controllers in sync mode. The service ends a search after two minutes;
    // starting it again keeps it going.
    Q_INVOKABLE void startSearch();
    Q_INVOKABLE void stopSearch();
    Q_INVOKABLE void switchOff(const QString &address);
    // Ends the controller with this player number; its Joy-Cons stay connected.
    Q_INVOKABLE void endUnit(int player);
    // The controllers with these two player numbers trade them.
    Q_INVOKABLE void swapPlayers(int player, int other);
    // Has the service measure this controller's motion sensors at rest (two seconds; it
    // must lie still). motionCalibrated says how it went.
    Q_INVOKABLE void calibrateMotion(const QString &address);
    // Has the service look for a Ring-Con on this right Joy-Con and read it, for the
    // next five seconds: ask again every second or so for as long as it is wanted.
    // ringFlex carries the readings; the Joy-Con's `ring` says whether one is attached.
    Q_INVOKABLE void watchRing(const QString &address, bool watch = true);

public slots:
    // The service announces changes of the units itself; battery levels only come with a
    // refresh.
    void refresh();

signals:
    void changed();
    // strength: the g it showed at rest before the correction; drift: degrees a second.
    void motionCalibrated(const QString &address, bool ok, double strength, double drift);
    // The ring's flex as the Joy-Con reports it, 66 times a second.
    void ringFlex(const QString &address, int raw);

private slots:
    void onMotionCalibrated(const QString &address, bool ok, double strength, double drift);

private:
    void call(const QString &method, const QVariantList &arguments = {});

    bool m_available = false;
    // What the service said last, to tell a real change from a repeated answer.
    QString m_described;
    bool m_searching = false;
    QVariantList m_units;
    QVariantList m_waiting;
};
