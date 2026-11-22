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
    m_heldFromBefore = enabled;
    emit enabledChanged();
}

void WebKeys::sendBack()
{
    if (!m_window)
        return;
    for (QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease}) {
        QKeyEvent escape(type, Qt::Key_Escape, Qt::NoModifier, QStringLiteral("\x1b"));
        QCoreApplication::sendEvent(m_window, &escape);
    }
}

bool WebKeys::eventFilter(QObject *, QEvent *event)
{
    if (!m_enabled || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease))
        return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    if (m_heldFromBefore) {
        // The rest of the press that opened the page (its repeats and its release) isn't
        // meant for the page: YouTube would take it as OK and start a video.
        if (event->type() != QEvent::KeyPress || key->isAutoRepeat())
            return true;
        m_heldFromBefore = false;
    }
    if (key->key() != Qt::Key_Back)
        return false;
    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat())
        emit backPressed();
    return true;
}
