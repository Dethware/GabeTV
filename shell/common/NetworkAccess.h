#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QQmlNetworkAccessManagerFactory>

#include <functional>

// The network layer for everything the shell loads, QML images included. It caches on
// disk (~/.cache/tvshell/network), and lets the parts of the shell adjust the requests
// for their own servers (photos adds the Immich API key) without this layer knowing them.
class NetworkAccessManager : public QNetworkAccessManager
{
public:
    // Adjusts a request in place, or leaves it alone. It runs on whichever thread the
    // manager lives on (the QML engine has one per image-loading thread), so it must only
    // read what it captured.
    using RequestRule = std::function<void(QNetworkRequest &request)>;

    // Rules are added at start, before the QML engine loads anything; they are not
    // guarded for being added later.
    static void addRequestRule(RequestRule rule);

    explicit NetworkAccessManager(QObject *parent = nullptr);

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override;

private:
    static QList<RequestRule> &rules();
};

// Installed on the QML engine, which creates one manager per loader thread.
class NetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override;
};
