#pragma once

#include <functional>

#include <QNetworkRequest>
#include <QString>
#include <QUrl>

// Where the Immich server is and how to talk to it, from ~/.config/tvshell/immich.conf:
//   [immich]
//   url=https://photos.example.org
//   apiKey=...
// The file holds a secret: keep it mode 600. The shell only reads; a key limited to
// album.read, memory.read, asset.read, asset.view and person.read is enough.
struct ImmichConfig
{
    QUrl url;
    QString apiKey;

    bool isValid() const { return url.isValid() && !url.isEmpty() && !apiKey.isEmpty(); }
    // Whether a request URL goes to the Immich server (and so may carry the key).
    bool isServerUrl(const QUrl &request) const;

    static ImmichConfig load();

    // For the shell's network layer (NetworkAccessManager::addRequestRule): requests to the
    // server get the API key, and an asset's images, which never change, come from the
    // cache when they are there. Lists (albums, memories) follow the server's cache headers.
    static std::function<void(QNetworkRequest &)> requestRule();
};
