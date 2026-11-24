#include "ShellService.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcService, "tvshell.service")

ShellService::ShellService(QObject *parent)
    : QObject(parent)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/local/tvshell"), this, QDBusConnection::ExportScriptableSlots)
        || !bus.registerService(QStringLiteral("local.tvshell")))
        qCWarning(lcService) << "not reachable on the session bus:" << bus.lastError().message();
    // Works whether or not the controller service is running yet: the match is by name.
    bus.connect(QStringLiteral("local.tvcontrollers"), QStringLiteral("/local/tvcontrollers"),
                QStringLiteral("local.tvcontrollers"), QStringLiteral("HomePressed"), this, SLOT(onControllerHome()));
}

void ShellService::onControllerHome()
{
    qCInfo(lcService) << "Home on a Joy-Con";
    emit controllerHomePressed();
}

void ShellService::LeaveApp()
{
    qCInfo(lcService) << "asked to leave the app on screen";
    emit leaveAppRequested();
}
