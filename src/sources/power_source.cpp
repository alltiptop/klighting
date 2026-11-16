// SPDX-License-Identifier: GPL-3.0-or-later
#include "power_source.h"
#include <KConfigGroup>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <algorithm>
#include <cmath>

namespace Ambient
{
namespace
{
    constexpr auto Service = "org.freedesktop.UPower";
    constexpr auto Properties = "org.freedesktop.DBus.Properties";
    constexpr int InitialRetryMs = 1000, MaximumRetryMs = 30000;
    const std::array<QString, 2> Paths { QStringLiteral("/org/freedesktop/UPower"),
        QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice") };
    const std::array<QString, 2> Interfaces { QStringLiteral("org.freedesktop.UPower"),
        QStringLiteral("org.freedesktop.UPower.Device") };
}
PowerSource::PowerSource(const QDBusConnection& bus, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_watcher(QString::fromLatin1(Service), bus, QDBusServiceWatcher::WatchForOwnerChange)
    , m_configWatcher(KConfigWatcher::create(KSharedConfig::openConfig("powerdevilrc")))
{
    for (int i = 0; i < 2; ++i) {
        m_retry[i].setSingleShot(true);
        connect(&m_retry[i], &QTimer::timeout, this, [this, i] { refresh(i); });
    }
    connect(m_configWatcher.data(), &KConfigWatcher::configChanged, this, [this] { reloadThreshold(); });
    reloadThreshold();
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString&, const QString&, const QString& owner) { ownerChanged(owner); });
    auto call = QDBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    call << QString::fromLatin1(Service);
    call.setAutoStartService(false);
    const int generation = m_generation;
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 2000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation] {
        QDBusPendingReply<QString> reply = *pending;
        pending->deleteLater();
        if (generation == m_generation)
            ownerChanged(reply.isError() ? QString() : reply.value());
    });
}
void PowerSource::reloadThreshold()
{
    const auto config = m_configWatcher->config();
    config->reparseConfiguration();
    m_lowThreshold
        = std::clamp(KConfigGroup(config, "BatteryManagement").readEntry("BatteryLowLevel", 10), 0, 100);
    publish();
}
void PowerSource::ownerChanged(const QString& owner)
{
    if (!m_owner.isEmpty())
        for (const auto& path : Paths)
            m_bus.disconnect(m_owner, path, Properties, "PropertiesChanged", this,
                SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    ++m_generation;
    m_owner = owner;
    m_properties = { };
    m_pending = { };
    m_revision = { };
    for (auto& retry : m_retry)
        retry.stop();
    m_retryDelay.fill(InitialRetryMs);
    m_seenPower = false;
    // Retain the last policy during a service outage. Discovery/reconnection
    // establishes a baseline and must never flash the LEDs as a plug-in event.
    m_status = owner.isEmpty() ? QStringLiteral("UPower unavailable; keeping last power state")
                               : QStringLiteral("Connecting to UPower");
    if (owner.isEmpty())
        return;
    for (int i = 0; i < 2; ++i) {
        m_bus.connect(owner, Paths[i], Properties, "PropertiesChanged", this,
            SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
        refresh(i);
    }
}
void PowerSource::refresh(int index)
{
    if (m_owner.isEmpty() || m_pending[index])
        return;
    m_retry[index].stop();
    m_pending[index] = true;
    const int generation = m_generation, revision = m_revision[index];
    auto call = QDBusMessage::createMethodCall(m_owner, Paths[index], Properties, "GetAll");
    call.setAutoStartService(false);
    call << Interfaces[index];
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 2000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, index, generation, revision] {
        QDBusPendingReply<QVariantMap> reply = *pending;
        pending->deleteLater();
        if (generation != m_generation)
            return;
        m_pending[index] = false;
        if (revision != m_revision[index]) {
            refresh(index); // A signal superseded this snapshot.
            return;
        }
        if (reply.isError()) {
            m_status = QStringLiteral("UPower read failed: %1").arg(reply.error().name());
            m_retry[index].start(m_retryDelay[index]);
            m_retryDelay[index] = std::min(m_retryDelay[index] * 2, MaximumRetryMs);
            return;
        }
        m_retryDelay[index] = InitialRetryMs;
        m_properties[index] = reply.value();
        publish();
    });
}
void PowerSource::propertiesChanged(const QString& interface, const QVariantMap& changed,
    const QStringList& invalidated, const QDBusMessage& message)
{
    if (m_owner.isEmpty() || message.service() != m_owner)
        return;
    const int index = interface == Interfaces[0] ? 0 : interface == Interfaces[1] ? 1 : -1;
    if (index < 0 || message.path() != Paths[index])
        return;
    ++m_revision[index];
    for (auto it = changed.cbegin(); it != changed.cend(); ++it)
        m_properties[index].insert(it.key(), it.value());
    for (const auto& key : invalidated)
        m_properties[index].remove(key);
    publish();
    if (!invalidated.isEmpty() || m_retry[index].isActive())
        refresh(index);
}
void PowerSource::publish()
{
    const auto battery = m_properties[0].value("OnBattery");
    if (battery.metaType().id() != QMetaType::Bool)
        return;
    const auto& display = m_properties[1];
    const auto percentage = display.value("Percentage");
    const double percent = percentage.toDouble();
    const bool validPercent = percentage.metaType().id() == QMetaType::Double && std::isfinite(percent)
        && percent >= 0 && percent <= 100;
    const int warning = display.value("WarningLevel").toInt();
    const bool low = display.value("IsPresent").toBool()
        && (validPercent ? percent <= m_lowThreshold : warning >= 3 && warning <= 5);
    const PowerState next { true, battery.toBool(), battery.toBool() && low };
    const bool plugged = m_seenPower && m_state.onBattery && !next.onBattery;
    m_seenPower = true;
    m_status = QStringLiteral("%1; low battery threshold=%2%")
                   .arg(next.onBattery ? (next.lowBattery ? "Low battery" : "Battery") : "AC power")
                   .arg(m_lowThreshold);
    if (next != m_state) {
        m_state = next;
        Q_EMIT stateChanged();
    }
    if (plugged)
        Q_EMIT powerConnected();
}
}
