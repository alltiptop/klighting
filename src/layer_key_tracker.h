// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "presets.h"
#include <KModifierKeyInfo>
#include <QObject>
#include <QPointer>
#include <QWindow>

namespace Ambient
{
// Observes input delivered to the settings window. Never grabs keys, inhibits
// shortcuts, accepts events, or retains typed text.
class LayerKeyTracker : public QObject
{
    Q_OBJECT
public:
    explicit LayerKeyTracker(QObject* parent = nullptr);
    ~LayerKeyTracker() override;
    void watch(QWindow* window);
    const ModifierState& state() const { return m_state; }
Q_SIGNALS:
    void stateChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    KModifierKeyInfo m_keyInfo;
    QPointer<QWindow> m_window, m_host;
    QMetaObject::Connection m_destroyed;
    QSet<quint32> m_pressed;
    Qt::KeyboardModifiers m_eventModifiers, m_latched, m_locked;
    bool m_active = false, m_capsLock = false;
    ModifierState m_state;
    void readLocks();
    void publish();
};
}
