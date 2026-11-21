#include "WebKeys.h"

#include <QCoreApplication>
#include <QKeyEvent>

WebKeys::WebKeys(QObject *parent)
    : QObject(parent)
{
}

void WebKeys::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
    emit windowChanged();
}

void WebKeys::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
}

bool WebKeys::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_enabled || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease))
        return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() != Qt::Key_Back)
        return false;
    QKeyEvent escape(event->type(), Qt::Key_Escape, key->modifiers(), QStringLiteral("\x1b"), key->isAutoRepeat());
    QCoreApplication::sendEvent(watched, &escape);
    return true;
}
