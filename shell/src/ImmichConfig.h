#pragma once

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
};
