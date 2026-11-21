#include "ImmichMediaProxy.h"

#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>

Q_LOGGING_CATEGORY(lcProxy, "tvshell.mediaproxy")

namespace {

// Request lines and headers from the media player are small.
constexpr qsizetype MaxRequestHeader = 16 * 1024;
// Don't buffer more than this towards a slow reader (the player reads as it plays).
constexpr qint64 MaxSocketBuffer = 4 * 1024 * 1024;

QByteArray statusText(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 206:
        return "Partial Content";
    case 404:
        return "Not Found";
    case 416:
        return "Range Not Satisfiable";
    default:
        return "Bad Gateway";
    }
}

} // namespace

ImmichMediaProxy::ImmichMediaProxy(QObject *parent)
    : QObject(parent)
    , m_config(ImmichConfig::load())
    , m_server(new QTcpServer(this))
    // A plain manager: no disk cache for videos, and the key is added here explicitly.
    , m_network(new QNetworkAccessManager(this))
{
    const quint64 random[2] = {QRandomGenerator::system()->generate64(), QRandomGenerator::system()->generate64()};
    m_token = QByteArray(reinterpret_cast<const char *>(random), sizeof(random)).toHex();
    connect(m_server, &QTcpServer::newConnection, this, &ImmichMediaProxy::accept);
    if (!m_server->listen(QHostAddress::LocalHost, 0))
        qCWarning(lcProxy) << "can't listen:" << m_server->errorString();
}

QUrl ImmichMediaProxy::videoUrl(const QString &assetId) const
{
    if (!m_config.isValid() || !m_server->isListening() || assetId.isEmpty())
        return {};
    return QUrl(QStringLiteral("http://127.0.0.1:%1/%2/video/%3")
                    .arg(m_server->serverPort())
                    .arg(QString::fromLatin1(m_token), assetId));
}

void ImmichMediaProxy::accept()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handle(socket); });
    }
}

void ImmichMediaProxy::handle(QTcpSocket *socket)
{
    // One request per connection; wait until the header is complete.
    if (socket->property("handled").toBool())
        return;
    QByteArray head = socket->property("head").toByteArray() + socket->readAll();
    if (!head.contains("\r\n\r\n")) {
        if (head.size() > MaxRequestHeader)
            socket->abort();
        else
            socket->setProperty("head", head);
        return;
    }
    socket->setProperty("handled", true);

    const QList<QByteArray> lines = head.left(head.indexOf("\r\n\r\n")).split('\n');
    const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
    static const QRegularExpression path(QStringLiteral("^/([0-9a-f]+)/video/([0-9a-fA-F-]+)$"));
    const auto match = path.match(QString::fromLatin1(request.value(1)));
    auto fail = [socket](int status) {
        socket->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status)
                      + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
    };
    if (request.value(0) != "GET" || !match.hasMatch() || match.captured(1).toLatin1() != m_token) {
        fail(404);
        return;
    }

    QUrl url = m_config.url;
    url.setPath(url.path() + QStringLiteral("/api/assets/%1/video/playback").arg(match.captured(2)));
    QNetworkRequest upstream(url);
    upstream.setRawHeader("x-api-key", m_config.apiKey.toUtf8());
    for (const QByteArray &line : lines) {
        if (line.toLower().startsWith("range:"))
            upstream.setRawHeader("Range", line.mid(6).trimmed());
    }
    qCDebug(lcProxy) << "GET" << match.captured(2) << upstream.rawHeader("Range");

    QNetworkReply *reply = m_network->get(upstream);
    // Back-pressure: with a bounded read buffer, the download pauses while we don't read it,
    // and we only read while the player keeps up.
    reply->setReadBufferSize(MaxSocketBuffer);
    QPointer<QTcpSocket> client(socket);
    connect(socket, &QTcpSocket::disconnected, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);

    // Status and headers once known, then the body as it arrives.
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, client] {
        if (!client || client->property("headerSent").toBool())
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray header = "HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status) + "\r\n";
        for (const char *name : {"Content-Type", "Content-Length", "Content-Range", "Accept-Ranges"}) {
            if (reply->hasRawHeader(name))
                header += QByteArray(name) + ": " + reply->rawHeader(name) + "\r\n";
        }
        header += "Connection: close\r\n\r\n";
        client->write(header);
        client->setProperty("headerSent", true);
    });
    auto forward = [reply, client] {
        if (client && client->property("headerSent").toBool() && client->bytesToWrite() < MaxSocketBuffer
            && reply->bytesAvailable() > 0)
            client->write(reply->readAll());
    };
    connect(reply, &QNetworkReply::readyRead, this, forward);
    connect(socket, &QTcpSocket::bytesWritten, reply, forward);
    connect(reply, &QNetworkReply::finished, this, [reply, client, fail] {
        if (!client)
            return;
        if (!client->property("headerSent").toBool()) {
            qCWarning(lcProxy) << "upstream failed:" << reply->errorString();
            fail(502);
            return;
        }
        client->write(reply->readAll());
        client->disconnectFromHost();
    });
}
