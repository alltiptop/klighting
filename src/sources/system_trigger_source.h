// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dbus_trigger_source.h"
#include "presets/presets.h"
#include <functional>
#include <map>
#include <memory>

namespace Ambient
{
class SystemTriggerSource : public QObject
{
    Q_OBJECT
public:
    explicit SystemTriggerSource(QObject* parent = nullptr);
    void configure(const QList<EffectPreset>& effects);
Q_SIGNALS:
    void valueChanged(const QString& id, const QVariant& value);
    void triggered(const QString& id, const QVariant& value);

private:
    void poll();
    void publish(const QString& type, const QVariant& value);
    void call(
        const QDBusConnection& bus, QDBusMessage message, std::function<void(const QDBusMessage&)> done);
    QMap<QString, TriggerOptions> m_options;
    std::map<QString, std::unique_ptr<DBusTriggerSource>> m_observers;
    QTimer m_poll;
    QDBusServiceWatcher m_systemOwners, m_sessionOwners;
    quint64 m_generation = 0;
    int m_pending = 0;
};
}
