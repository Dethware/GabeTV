#include "ControllerClient.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcControllers, "tvshell.controllers")

namespace {

const QString Service = QStringLiteral("local.tvcontrollers");
const QString Path = QStringLiteral("/local/tvcontrollers");

} // namespace

ControllerClient::ControllerClient(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::sessionBus().connect(Service, Path, Service, QStringLiteral("Changed"), this, SLOT(refresh()));
    refresh();
}

void ControllerClient::refresh()
{
    const QDBusMessage describe = QDBusMessage::createMethodCall(Service, Path, Service, QStringLiteral("Describe"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(describe), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<QString> reply = *finished;
        finished->deleteLater();
        const bool available = !reply.isError();
        if (available != m_available)
            qCInfo(lcControllers) << (available ? "the controller service is there" : "the controller service doesn't answer");
        const QVariantMap all = available ? QJsonDocument::fromJson(reply.value().toUtf8()).object().toVariantMap()
                                          : QVariantMap();
        m_available = available;
        m_searching = all.value(QStringLiteral("searching")).toBool();
        m_units = all.value(QStringLiteral("units")).toList();
        m_waiting = all.value(QStringLiteral("unassigned")).toList();
        emit changed();
    });
}

void ControllerClient::call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Service, Path, Service, method);
    message.setArguments(arguments);
    QDBusConnection::sessionBus().asyncCall(message);
}

void ControllerClient::startSearch()
{
    call(QStringLiteral("StartSearch"));
}

void ControllerClient::stopSearch()
{
    call(QStringLiteral("StopSearch"));
}

void ControllerClient::switchOff(const QString &address)
{
    call(QStringLiteral("SwitchOff"), {address});
}

void ControllerClient::endUnit(int player)
{
    call(QStringLiteral("EndUnit"), {player});
}
