// SPDX-License-Identifier: GPL-3.0-or-later
#include "notification_source.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUuid>

namespace Ambient
{
namespace
{
    constexpr auto Service = "org.freedesktop.Notifications";
    constexpr auto Path = "/org/freedesktop/Notifications";
    QDBusMessage request(const QString& owner, const QString& method)
    {
        auto message = QDBusMessage::createMethodCall(
            owner, QString::fromLatin1(Path), QStringLiteral("org.kde.NotificationManager"), method);
        message.setAutoStartService(false);
        return message;
    }
}
NotificationSource::NotificationSource(QObject* parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("ambient-notifications-%1").arg(QUuid::createUuid().toString()))
    , m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName))
    , m_watcher(QString::fromLatin1(Service), m_bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString&, const QString&, const QString& owner) {
            if (m_enabled)
                ownerChanged(owner);
        });
}
NotificationSource::~NotificationSource()
{
    setEnabled(false);
    QDBusConnection::disconnectFromBus(m_connectionName);
}
void NotificationSource::setStatus(const QString& status)
{
    if (m_status == status)
        return;
    m_status = status;
    Q_EMIT statusChanged();
}
void NotificationSource::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    ++m_generation;
    if (!enabled) {
        if (!m_owner.isEmpty())
            m_bus.asyncCall(request(m_owner, QStringLiteral("UnRegisterWatcher")));
        m_bus.unregisterObject(QStringLiteral("/NotificationWatcher"));
        m_owner.clear();
        setStatus(QStringLiteral("Disabled"));
        return;
    }
    if (!m_bus.isConnected()
        || !m_bus.registerObject(
            QStringLiteral("/NotificationWatcher"), this, QDBusConnection::ExportScriptableSlots)) {
        setStatus(QStringLiteral("Notification D-Bus connection unavailable"));
        return;
    }
    // Asynchronous discovery does not block the compositor or start a server.
    const auto generation = m_generation;
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("GetNameOwner"));
    call << QString::fromLatin1(Service);
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 2000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, generation, pending] {
        QDBusPendingReply<QString> reply = *pending;
        pending->deleteLater();
        if (m_enabled && m_generation == generation)
            ownerChanged(reply.isError() ? QString() : reply.value());
    });
}
void NotificationSource::ownerChanged(const QString& owner)
{
    m_owner = owner;
    const auto generation = ++m_generation;
    if (owner.isEmpty()) {
        setStatus(QStringLiteral("Waiting for Plasma notifications"));
        return;
    }
    setStatus(QStringLiteral("Connecting to Plasma notifications"));
    auto* pending = new QDBusPendingCallWatcher(
        m_bus.asyncCall(request(owner, QStringLiteral("RegisterWatcher")), 2000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, generation, pending] {
        QDBusPendingReply<> reply = *pending;
        pending->deleteLater();
        if (!m_enabled || m_generation != generation)
            return;
        setStatus(reply.isError()
                ? QStringLiteral("Notification watcher unavailable: %1").arg(reply.error().name())
                : QStringLiteral("Connected to Plasma notifications"));
    });
}
void NotificationSource::Notify(uint, const QString& appName, uint, const QString&, const QString&,
    const QString&, const QStringList&, const QVariantMap& hints, int)
{
    if (!m_enabled || m_owner.isEmpty() || !calledFromDBus() || message().service() != m_owner)
        return;
    auto text = [&](const char* key) { return hints.value(QString::fromLatin1(key)).toString().left(256); };
    NotificationEvent event { appName.left(256), text("desktop-entry"), text("category"),
        text("x-kde-eventId"), 1 };
    if (hints.contains(QStringLiteral("urgency"))) {
        bool ok;
        const auto urgency = hints.value(QStringLiteral("urgency")).toInt(&ok);
        event.urgency = ok && urgency >= 0 && urgency <= 2 ? urgency : -1;
    }
    Q_EMIT received(event);
}
void NotificationSource::CloseNotification(uint) { }
}
