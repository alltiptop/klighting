// SPDX-License-Identifier: GPL-3.0-or-later
#include "condition_recorder.h"
#include "presets.h"
#include <QCoreApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <algorithm>
#include <linux/input-event-codes.h>
namespace Ambient
{
namespace
{
    QString physicalCondition(quint32 code)
    {
        switch (code) {
        case KEY_LEFTSHIFT:
            return QStringLiteral("shift-left");
        case KEY_RIGHTSHIFT:
            return QStringLiteral("shift-right");
        case KEY_LEFTCTRL:
            return QStringLiteral("ctrl-left");
        case KEY_RIGHTCTRL:
            return QStringLiteral("ctrl-right");
        case KEY_LEFTALT:
            return QStringLiteral("alt-left");
        case KEY_RIGHTALT:
            return QStringLiteral("alt-right");
        case KEY_LEFTMETA:
            return QStringLiteral("super-left");
        case KEY_RIGHTMETA:
            return QStringLiteral("super-right");
        case KEY_COMPOSE:
        case KEY_MENU:
            return QStringLiteral("menu");
        case KEY_FN:
            return QStringLiteral("fn");
        default:
            return QStringLiteral("key:%1").arg(code);
        }
    }
}
ConditionRecorder::ConditionRecorder(QObject* parent)
    : QObject(parent)
    , m_recorder(new KKeySequenceRecorder(nullptr, this))
{
    connect(m_recorder, &KKeySequenceRecorder::recordingChanged, this, [this] {
        if (m_recording && !m_recorder->isRecording())
            stopRecording(false);
    });
}
ConditionRecorder::~ConditionRecorder() { cancel(); }
QString ConditionRecorder::hint() const
{
    if (m_recording)
        return tr("Hold the keys together, then release them. Esc cancels.");
    if (m_invalid)
        return tr("Could not record these keys (maximum: 8). The previous combination was kept.");
    return QString();
}
void ConditionRecorder::start(QWindow* window)
{
    cancel();
    m_window = window;
    m_captured.clear();
    m_held.clear();
    m_releasing = m_invalid = false;
    // kcmshell6 embeds Quick in a QQuickWidget. Inhibit shortcuts on its real
    // top-level window while retaining the Quick window for forwarded key events.
    m_inhibitionWindow = window;
    if (auto* quick = qobject_cast<QQuickWindow*>(window))
        if (auto* host = QQuickRenderControl::renderWindowFor(quick))
            m_inhibitionWindow = host;
    m_recorder->setWindow(m_inhibitionWindow);
    m_recorder->startRecording();
    m_recording = m_recorder->isRecording();
    if (m_recording)
        QCoreApplication::instance()->installEventFilter(this);
    Q_EMIT changed();
}
void ConditionRecorder::cancel() { stopRecording(false); }
void ConditionRecorder::stopRecording(bool accept)
{
    if (!m_recording)
        return;
    m_recording = false;
    QCoreApplication::instance()->removeEventFilter(this);
    m_recorder->cancelRecording();
    if (accept && !m_invalid && !m_captured.isEmpty())
        Q_EMIT accepted(m_captured);
    Q_EMIT changed();
}
bool ConditionRecorder::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_recording)
        return false;
    const auto* item = qobject_cast<QQuickItem*>(watched);
    const bool ours
        = watched == m_window || watched == m_inhibitionWindow || (item && item->window() == m_window);
    if (!ours)
        return false;
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Close
        || (event->type() == QEvent::Hide && (watched == m_window || watched == m_inhibitionWindow))) {
        stopRecording(false);
        return false;
    }
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::ContextMenu) {
        event->accept();
        return true;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return false;
    auto* key = static_cast<QKeyEvent*>(event);
    event->accept();
    if (key->isAutoRepeat())
        return true;
    // Qt's Wayland and X11 backends both expose XKB codes (evdev + 8).
    const quint32 code = key->nativeScanCode() >= 8 ? key->nativeScanCode() - 8 : 0;
    if (key->key() == Qt::Key_Escape) {
        if (event->type() == QEvent::KeyRelease)
            stopRecording(false);
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        m_held.insert(code);
        if (!m_releasing) {
            const auto id = physicalCondition(code);
            const auto known = availableConditions();
            const bool supported = std::any_of(
                known.begin(), known.end(), [&id](const auto& entry) { return entry.first == id; });
            if (!supported || (m_captured.size() >= 8 && !m_captured.contains(id)))
                m_invalid = true;
            else if (!m_captured.contains(id))
                m_captured.append(id);
            Q_EMIT changed();
        }
    } else if (m_held.remove(code)) {
        // Freeze on the first release: a sequence such as Ctrl+A, Ctrl+B
        // must not become a chord requiring A and B simultaneously.
        m_releasing = true;
        if (m_held.isEmpty())
            stopRecording(true);
    }
    return true;
}
}
