// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "notification_effect.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QVariantMap>

namespace Ambient
{
// A dedicated connection keeps /NotificationWatcher independent of other KWin
// clients. Plasma owns the notification service throughout this object's life.
class NotificationSource : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")
public:
    explicit NotificationSource(QObject* parent = nullptr);
    ~NotificationSource() override;
    void setEnabled(bool enabled);
    QString status() const { return m_status; }
Q_SIGNALS:
    void received(const Ambient::NotificationEvent& event);
    void statusChanged();
public Q_SLOTS:
    Q_SCRIPTABLE void Notify(uint id, const QString& appName, uint replacesId, const QString& icon,
        const QString& summary, const QString& body, const QStringList& actions, const QVariantMap& hints,
        int timeout);
    Q_SCRIPTABLE void CloseNotification(uint id);

private:
    void ownerChanged(const QString& owner);
    void setStatus(const QString& status);
    QString m_connectionName, m_owner, m_status = QStringLiteral("Disabled");
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    bool m_enabled = false;
    int m_generation = 0;
};
}
Q_DECLARE_METATYPE(Ambient::NotificationEvent)
