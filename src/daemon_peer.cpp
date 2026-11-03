// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_peer.h"
#include "lighting_ipc.h"
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace Ambient
{
DaemonPeer::DaemonPeer(QObject* parent)
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
    , m_watcher(QString::fromLatin1(Ipc::Service), m_bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString&, const QString&, const QString& owner) { ownerChanged(owner); });
    auto call = QDBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    call << QString::fromLatin1(Ipc::Service);
    const int generation = m_generation;
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation] {
        const QDBusPendingReply<QString> reply = *pending;
        pending->deleteLater();
        if (generation == m_generation)
            ownerChanged(reply.isError() ? QString() : reply.value());
    });
}
void DaemonPeer::ownerChanged(const QString& owner)
{
    ++m_generation;
    m_owner = owner;
    Q_EMIT changed();
}
void DaemonPeer::send(
    const char* path, const char* interface, const char* signal, const QVariantList& arguments)
{
    if (!connected())
        return;
    auto message = QDBusMessage::createTargetedSignal(
        m_owner, QLatin1String(path), QLatin1String(interface), QLatin1String(signal));
    message.setArguments(arguments);
    m_bus.send(message);
}
}
