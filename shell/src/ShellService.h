#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// What other programs can ask of the shell, on the session bus:
//
//   busctl --user call local.tvshell /local/tvshell local.tvshell LeaveApp
//
// LeaveApp: the managed app on screen wants the shell back (Back at Jellyfin's top level).
// It stays loaded, as after Home.
//
// It also listens to the controller service (controllers/): Home on a Joy-Con arrives as
// that service's HomePressed signal, since no program but the service sees the Joy-Cons.
class ShellService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_CLASSINFO("D-Bus Interface", "local.tvshell")

public:
    explicit ShellService(QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE void LeaveApp();

signals:
    void leaveAppRequested();
    // Home on a Joy-Con: the same as Home on the remote.
    void controllerHomePressed();

private slots:
    void onControllerHome();
};
