#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QWindow>

// While enabled, the shell's Back key (controller B, the remote's Back) reaches the window as
// Escape, which web TV apps such as youtube.com/tv take as "back".
class WebKeys : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit WebKeys(QObject *parent = nullptr);

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

signals:
    void windowChanged();
    void enabledChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QWindow> m_window;
    bool m_enabled = false;
};
