// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "desktop_source.h"
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QTimer>

namespace Ambient
{
class KWinDesktopSource : public DesktopSource
{
    Q_OBJECT
public:
    explicit KWinDesktopSource(
        const QDBusConnection& bus = QDBusConnection::sessionBus(), QObject* parent = nullptr);
    void setTrackedKeys(const QSet<quint32>& keys) override;
    QString status() const override { return m_status; }
private Q_SLOTS:
    void desktopState(const QVariantMap& state);

private:
    void ownerChanged(const QString& owner);
    void refresh();
    void configure();
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QTimer m_poll;
    QString m_owner, m_status = QStringLiteral("Desktop integration unavailable");
    QByteArray m_trackedKeys;
    int m_generation = 0;
    bool m_reading = false, m_available = false, m_triedIntegration = false;
};
}
