#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QThread;

// One Switch game file and what the shell shows of it: its name and its icon, read from the
// file by the shell itself (format/readGame) with the keys from the newest folder in
// ~/Games/Emulators/Switch/keys/, and kept in a GameCache. A file not in the cache yet is
// read on a thread of its own; name and icon stay empty until then, or if it can't be read.
//
// Knows nothing about emulators.
class SwitchGame : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The game file: absolute, or relative to ~/Games/Emulators/Switch/.
    Q_PROPERTY(QString file READ file WRITE setFile NOTIFY fileChanged)
    Q_PROPERTY(QString titleId READ titleId NOTIFY infoChanged)
    Q_PROPERTY(QString name READ name NOTIFY infoChanged)
    // The cached icon.jpg (256×256), or empty.
    Q_PROPERTY(QUrl icon READ icon NOTIFY infoChanged)

public:
    explicit SwitchGame(QObject *parent = nullptr);
    ~SwitchGame() override;

    QString file() const { return m_file; }
    void setFile(const QString &file);

    QString titleId() const { return m_titleId; }
    QString name() const { return m_name; }
    QUrl icon() const { return m_icon; }

signals:
    void fileChanged();
    void infoChanged();

private:
    void load();
    void read(const QString &path);

    QString m_file;
    QString m_titleId;
    QString m_name;
    QUrl m_icon;

    QThread *m_reader = nullptr;
    bool m_loadWhenRead = false; // the file changed while the old one was being read
};
