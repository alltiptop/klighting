// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "integration_sources.h"
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QTimer>

namespace Ambient
{
// KWin-specific legacy-controller detection updates the separately injected guard.
class KWinCaptureSource : public CaptureSource
{
    Q_OBJECT
public:
    explicit KWinCaptureSource(const std::shared_ptr<HardwareControlGuard>& guard,
        const QDBusConnection& bus = QDBusConnection::sessionBus(), QObject* parent = nullptr);
    ~KWinCaptureSource() override;
    void requestCapture(int fps, const QVariantMap& options) override;
    CaptureState state() const override { return m_state; }
private Q_SLOTS:
    void screenSample(const QByteArray& rgba);

private:
    void ownerChanged(const QString& owner);
    void refresh();
    void captureState(bool safe, const CaptureState& state);
    void configureCapture();
    void resetConfiguration();
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QTimer m_poll;
    const std::shared_ptr<HardwareControlGuard> m_guard;
    QString m_owner;
    int m_generation = 0, m_fps = 0, m_configurationEpoch = 0, m_configuredFps = -1;
    QVariantMap m_options, m_configuredOptions;
    CaptureState m_state;
    bool m_reading = false, m_configuring = false;
};
}
