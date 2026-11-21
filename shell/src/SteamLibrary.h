#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// Installed Steam games, read from Steam's own files (no Steam API involved):
// steamapps/libraryfolders.vdf for the library folders, appmanifest_*.acf per installed app,
// appcache/librarycache/<appid>/ for the artwork. Launching goes through steam:// URLs.
class SteamLibrary : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        AppIdRole = Qt::UserRole + 1,
        NameRole,
        CoverRole,
    };
    Q_ENUM(Role)

    explicit SteamLibrary(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_games.size()); }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void launch(const QString &appId) const;

signals:
    void countChanged();

private:
    struct Game {
        QString appId;
        QString name;
        QUrl cover;
    };

    QStringList libraryFolders() const;

    QString m_steamRoot;
    QList<Game> m_games;
};
