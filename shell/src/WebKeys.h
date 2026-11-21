#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QWindow>

// While enabled, the shell's Back key (controller B, the remote's Back) is taken from the
// window and reported as backPressed(); sendBack() then hands it to the page as Escape, which
// web TV apps such as youtube.com/tv take as "back". So QML can look at the page first.
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

    // Escape (press and release) to the window's focused item.
    Q_INVOKABLE void sendBack();

signals:
    void windowChanged();
    void enabledChanged();
    void backPressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QWindow> m_window;
    bool m_enabled = false;
};
