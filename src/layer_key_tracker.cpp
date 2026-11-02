// SPDX-License-Identifier: GPL-3.0-or-later
#include "layer_key_tracker.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <linux/input-event-codes.h>

namespace Ambient
{
namespace
{
    Qt::KeyboardModifier modifierFor(int key)
    {
        switch (key) {
        case Qt::Key_Shift:
            return Qt::ShiftModifier;
        case Qt::Key_Control:
            return Qt::ControlModifier;
        case Qt::Key_Alt:
            return Qt::AltModifier;
        case Qt::Key_AltGr:
            return Qt::GroupSwitchModifier;
        case Qt::Key_Meta:
            return Qt::MetaModifier;
        default:
            return Qt::NoModifier;
        }
    }
}
LayerKeyTracker::LayerKeyTracker(QObject* parent)
    : QObject(parent)
{
    connect(&m_keyInfo, &KModifierKeyInfo::keyLocked, this, [this](Qt::Key key, bool locked) {
        if (key == Qt::Key_CapsLock)
            m_capsLock = locked;
        const auto flag = modifierFor(key);
        m_locked.setFlag(flag, locked);
        if (!locked)
            m_eventModifiers &= ~Qt::KeyboardModifiers(flag);
        publish();
    });
    connect(&m_keyInfo, &KModifierKeyInfo::keyLatched, this, [this](Qt::Key key, bool latched) {
        const auto flag = modifierFor(key);
        m_latched.setFlag(flag, latched);
        if (!latched)
            m_eventModifiers &= ~Qt::KeyboardModifiers(flag);
        publish();
    });
}
LayerKeyTracker::~LayerKeyTracker() { QCoreApplication::instance()->removeEventFilter(this); }
void LayerKeyTracker::readLocks()
{
    m_capsLock = m_keyInfo.isKeyLocked(Qt::Key_CapsLock);
    m_latched = m_locked = Qt::NoModifier;
    for (const auto key : { Qt::Key_Shift, Qt::Key_Control, Qt::Key_Alt, Qt::Key_AltGr, Qt::Key_Meta }) {
        m_latched.setFlag(modifierFor(key), m_keyInfo.isKeyLatched(key));
        m_locked.setFlag(modifierFor(key), m_keyInfo.isKeyLocked(key));
    }
}
void LayerKeyTracker::watch(QWindow* window)
{
    if (window == m_window && window)
        return;
    QCoreApplication::instance()->removeEventFilter(this);
    disconnect(m_destroyed);
    m_window = m_host = window;
    if (auto* quick = qobject_cast<QQuickWindow*>(window))
        if (auto* host = QQuickRenderControl::renderWindowFor(quick))
            m_host = host;
    m_pressed.clear();
    m_eventModifiers = Qt::NoModifier;
    m_active = m_host && m_host->isActive();
    if (window) {
        m_destroyed = connect(window, &QObject::destroyed, this, [this] { watch(nullptr); });
        readLocks();
        m_eventModifiers = QGuiApplication::keyboardModifiers();
        QCoreApplication::instance()->installEventFilter(this);
    }
    publish();
}
void LayerKeyTracker::publish()
{
    const ModifierState next = m_active && m_window
        ? ModifierState { m_pressed, m_eventModifiers | m_latched | m_locked, m_capsLock }
        : ModifierState { };
    if (next == m_state)
        return;
    m_state = next;
    Q_EMIT stateChanged();
}
bool LayerKeyTracker::eventFilter(QObject* watched, QEvent* event)
{
    const auto* item = qobject_cast<QQuickItem*>(watched);
    if (!m_window || (watched != m_window && watched != m_host && (!item || item->window() != m_window)))
        return false;
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Close
        || (event->type() == QEvent::Hide && (watched == m_window || watched == m_host))) {
        m_active = false;
        m_pressed.clear();
        m_eventModifiers = Qt::NoModifier;
        publish();
    } else if (event->type() == QEvent::WindowActivate) {
        m_active = true;
        m_pressed.clear();
        m_eventModifiers = QGuiApplication::keyboardModifiers();
        readLocks();
        publish();
    } else if (m_active
        && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->isAutoRepeat())
            return false;
        const bool down = event->type() != QEvent::KeyRelease;
        quint32 code = key->nativeScanCode() >= 8 ? key->nativeScanCode() - 8 : 0;
        if (!code && key->key() == Qt::Key_CapsLock)
            code = KEY_CAPSLOCK;
        if (code > 0 && code <= KEY_MAX) {
            // Qt may deliver the same press to both the widget host and Quick.
            if (code == KEY_CAPSLOCK && down && !m_pressed.contains(code)
                && !m_keyInfo.knowsKey(Qt::Key_CapsLock))
                m_capsLock = !m_capsLock;
            if (down)
                m_pressed.insert(code);
            else
                m_pressed.remove(code);
        }
        m_eventModifiers = key->modifiers();
        // Qt's modifier bit for the key being pressed/released may reflect the
        // state before that event. Physical held keys are checked separately.
        const auto flag = modifierFor(key->key());
        if (flag != Qt::NoModifier)
            m_eventModifiers.setFlag(flag, down);
        publish();
    }
    return false;
}
}
