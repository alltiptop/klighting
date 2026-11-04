// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "desktop_source.h"
#include "integration_startup_guard.h"
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
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
    QVariantMap info() const override { return m_info; }
    bool canRetry() const override;
    QString retry() override;
private Q_SLOTS:
    void desktopState(const QVariantMap& state);

private:
    void ownerChanged(const QString& owner);
    void refresh();
    void configure();
    void resetConfiguration();
    bool acceptState(const QVariantMap& state);
    QString loadIntegration(bool manual = false);
    IntegrationStartupGuard m_startup;
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QTimer m_poll;
    QElapsedTimer m_stable;
    QString m_owner, m_status = QStringLiteral("Desktop integration unavailable");
    QByteArray m_trackedKeys, m_configuredKeys;
    QVariantMap m_info;
    int m_generation = 0, m_revision = 0, m_configurationEpoch = 0;
    bool m_reading = false, m_available = false, m_triedIntegration = false;
    bool m_loading = false, m_recovered = false, m_configuring = false;
};
}
