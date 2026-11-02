// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "power_state.h"
#include <KConfigWatcher>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QVariantMap>
#include <array>

namespace Ambient
{
// Observe system power without changing power policy or starting services.
class PowerSource : public QObject
{
    Q_OBJECT
public:
    explicit PowerSource(
        const QDBusConnection& bus = QDBusConnection::systemBus(), QObject* parent = nullptr);
    const PowerState& state() const { return m_state; }
    QString status() const { return m_status; }
    void reloadThreshold();
Q_SIGNALS:
    void stateChanged();
    void powerConnected();
private Q_SLOTS:
    void propertiesChanged(const QString& interface, const QVariantMap& changed,
        const QStringList& invalidated, const QDBusMessage& message);

private:
    void ownerChanged(const QString& owner);
    void refresh(int index);
    void publish();
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    KConfigWatcher::Ptr m_configWatcher;
    QString m_owner, m_status = QStringLiteral("Waiting for UPower");
    std::array<QVariantMap, 2> m_properties;
    std::array<int, 2> m_revision { };
    std::array<bool, 2> m_pending { };
    PowerState m_state;
    int m_generation = 0, m_lowThreshold = 10;
    bool m_seenPower = false;
};
}
