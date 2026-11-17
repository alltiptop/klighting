// SPDX-License-Identifier: GPL-3.0-or-later
#include "dbus_trigger_source.h"
#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace Ambient
{
DBusTriggerSource::DBusTriggerSource(
    const TriggerOptions& options, const QDBusConnection& bus, QObject* parent)
    : QObject(parent)
    , m_options(options)
    , m_bus(bus)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString&, const QString&, const QString& owner) { ownerChanged(owner); });
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &DBusTriggerSource::refresh);
    if (options.service.isEmpty() || options.path.isEmpty() || options.interface.isEmpty()
        || options.member.isEmpty())
        return;
    m_watcher.setConnection(bus);
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_watcher.addWatchedService(options.service);
    auto message = QDBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    message << options.service;
    auto* call = new QDBusPendingCallWatcher(bus.asyncCall(message, 1500), this);
    const auto generation = m_generation;
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, call, generation] {
        const QDBusPendingReply<QString> reply = *call;
        call->deleteLater();
        if (generation == m_generation)
            ownerChanged(reply.isError() ? QString() : reply.value());
    });
}
void DBusTriggerSource::ownerChanged(const QString& owner)
{
    ++m_generation;
    m_pending = m_dirty = false;
    const auto
        interface = m_options.signal ? m_options.interface : QStringLiteral("org.freedesktop.DBus.Properties");
    const auto member = m_options.signal ? m_options.member : QStringLiteral("PropertiesChanged");
    if (!m_owner.isEmpty())
        m_bus.disconnect(m_owner, m_options.path, interface, member, this, SLOT(received(QDBusMessage)));
    m_owner = owner;
    Q_EMIT valueChanged({ });
    m_retry.stop();
    if (owner.isEmpty())
        return;
    m_bus.connect(owner, m_options.path, interface, member, this, SLOT(received(QDBusMessage)));
    if (!m_options.signal) {
        m_retry.start();
        refresh();
    }
}
void DBusTriggerSource::received(const QDBusMessage& message)
{
    if (message.service() != m_owner || m_owner.isEmpty())
        return;
    if (m_options.signal) {
        QVariant value = message.arguments().isEmpty() ? QVariant(true) : message.arguments().first();
        if (value.metaType() == QMetaType::fromType<QDBusVariant>())
            value = qvariant_cast<QDBusVariant>(value).variant();
        Q_EMIT triggered(value);
    } else if (message.arguments().size() == 3
        && message.arguments().first().toString() == m_options.interface) {
        const auto changed = qdbus_cast<QVariantMap>(message.arguments().at(1));
        const auto invalidated = qdbus_cast<QStringList>(message.arguments().at(2));
        if (!changed.contains(m_options.member) && !invalidated.contains(m_options.member))
            return;
        ++m_revision;
        // A pending reply predating this signal cannot overwrite the updated state.
        if (changed.contains(m_options.member)) {
            Q_EMIT valueChanged(changed.value(m_options.member));
        } else {
            Q_EMIT valueChanged({ });
            refresh();
        }
    }
}
void DBusTriggerSource::refresh()
{
    if (m_owner.isEmpty() || m_options.signal)
        return;
    if (m_pending) {
        m_dirty = true;
        return;
    }
    m_pending = true;
    const auto generation = m_generation, revision = m_revision;
    auto message
        = QDBusMessage::createMethodCall(m_owner, m_options.path, "org.freedesktop.DBus.Properties", "Get");
    message.setAutoStartService(false);
    message << m_options.interface << m_options.member;
    auto* call = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 1500), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, call, generation, revision] {
        const QDBusPendingReply<QDBusVariant> reply = *call;
        call->deleteLater();
        if (generation != m_generation)
            return;
        m_pending = false;
        if (revision == m_revision)
            Q_EMIT valueChanged(reply.isError() ? QVariant() : reply.value().variant());
        if (m_dirty) {
            m_dirty = false;
            refresh();
        }
    });
}
}
