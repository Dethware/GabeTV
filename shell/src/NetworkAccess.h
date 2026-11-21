#pragma once

#include "ImmichConfig.h"

#include <QNetworkAccessManager>
#include <QQmlNetworkAccessManagerFactory>

// The network layer for everything the shell loads, QML images included.
// Adds the Immich API key to requests for the Immich server only, and caches on disk
// (~/.cache/tvshell/network).
class NetworkAccessManager : public QNetworkAccessManager
{
public:
    explicit NetworkAccessManager(QObject *parent = nullptr);

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override;

private:
    ImmichConfig m_immich;
};

// Installed on the QML engine, which creates one manager per loader thread.
class NetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override;
};
