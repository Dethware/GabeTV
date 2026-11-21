#pragma once

#include "ImmichConfig.h"

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

// A tiny HTTP proxy on 127.0.0.1 for Immich videos, because Qt's media player can't send
// the API key header. The player gets http://127.0.0.1:<port>/<token>/video/<assetId>; the
// proxy forwards it (Range included, for seeking) to Immich's /api/assets/<id>/video/playback
// with the key and streams the answer back. The random token keeps other local programs out.
class ImmichMediaProxy : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ImmichMediaProxy(QObject *parent = nullptr);

    Q_INVOKABLE QUrl videoUrl(const QString &assetId) const;

private:
    void accept();
    void handle(QTcpSocket *socket);

    ImmichConfig m_config;
    QTcpServer *m_server;
    QNetworkAccessManager *m_network;
    QByteArray m_token;
};
