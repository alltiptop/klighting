// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "triggers/trigger_options.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QTimer>

namespace Ambient
{
class DBusTriggerSource : public QObject
{
    Q_OBJECT
public:
    DBusTriggerSource(const TriggerOptions& options, const QDBusConnection& bus, QObject* parent = nullptr);
Q_SIGNALS:
    void valueChanged(const QVariant& value);
    void triggered(const QVariant& value);
private Q_SLOTS:
    void received(const QDBusMessage& message);

private:
    void ownerChanged(const QString& owner);
    void refresh();
    TriggerOptions m_options;
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QTimer m_retry;
    QString m_owner;
    quint64 m_generation = 0, m_revision = 0;
    bool m_pending = false, m_dirty = false;
};
}
