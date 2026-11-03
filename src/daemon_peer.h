// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>

namespace Ambient
{
// Compositor-side connection: never starts the daemon and never broadcasts screen/input data.
class DaemonPeer : public QObject
{
    Q_OBJECT
public:
    explicit DaemonPeer(QObject* parent = nullptr);
    bool isOwner(const QString& sender) const { return !m_owner.isEmpty() && sender == m_owner; }
    bool connected() const { return !m_owner.isEmpty(); }
    void send(const char* path, const char* interface, const char* signal, const QVariantList& arguments);
Q_SIGNALS:
    void changed();

private:
    void ownerChanged(const QString& owner);
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QString m_owner;
    int m_generation = 0;
};
}
