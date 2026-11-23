#include "Bluetooth.h"
#include "ControllerService.h"
#include "DeviceWatcher.h"
#include "UnitManager.h"

#include <QCoreApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMain, "tvcontrollers")

// The TV's controller service: the only program that talks to the Joy-Cons. See
// DEVELOPMENT.md for how the parts fit.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("tvcontrollers"));

    DeviceWatcher watcher;
    UnitManager units(&watcher);
    Bluetooth bluetooth;
    ControllerService service(&units, &bluetooth);
    // A switched-off Joy-Con leaves a dead connection that the machine keeps for 20 s, and
    // until it is gone a button press can't reconnect. Drop it at once.
    QObject::connect(&watcher, &DeviceWatcher::joyConSwitchedOff, &bluetooth, &Bluetooth::switchOff);
    QObject::connect(&watcher, &DeviceWatcher::joyConAdded, &bluetooth,
                     [&bluetooth](JoyCon *joyCon) { bluetooth.follow(joyCon->address()); });

    qCInfo(lcMain) << "started";
    return app.exec();
}
