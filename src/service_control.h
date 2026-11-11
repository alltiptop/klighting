// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "component_info.h"
#include <QObject>
#include <QTimer>

namespace Ambient
{
class ServiceControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool compatible READ compatible NOTIFY changed)
    Q_PROPERTY(bool desktopCanRetry READ desktopCanRetry NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(QVariantMap microphoneStatus READ microphoneStatus NOTIFY changed)
public:
    explicit ServiceControl(QObject* parent = nullptr);
    explicit ServiceControl(const InstallationPaths& paths, QObject* parent = nullptr);
    bool busy() const { return m_busy; }
    bool running() const { return m_running; }
    bool compatible() const { return m_compatible; }
    bool desktopCanRetry() const { return m_desktopCanRetry; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString diagnostics() const { return m_diagnostics; }
    QVariantMap microphoneStatus() const { return m_microphoneStatus; }
    void reload();
    void refresh();
    Q_INVOKABLE void retryDesktopIntegration();
Q_SIGNALS:
    void changed();
    void finished();

private:
    void updateDiagnostics(const QVariantMap& status);
    const InstallationPaths m_paths;
    QTimer m_timer;
    QString m_status, m_error, m_diagnostics;
    QVariantMap m_microphoneStatus;
    bool m_busy = false, m_running = false, m_reading = false;
    bool m_desktopCanRetry = false;
    bool m_compatible = false;
    int m_generation = 0;
};
}
