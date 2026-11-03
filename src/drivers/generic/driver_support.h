// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_driver.h"
#include <QElapsedTimer>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <optional>

namespace Ambient::Generic
{
QByteArray readFile(const QString& path, qint64 limit = 65536);
QString digest(const QByteArray& value);
QString usbParent(const QString& path);
QStringList deviceClaims(const QString& path, bool keyboard);
bool conflicts(const QStringList& claims, const QStringList& context);
QList<DeviceKey> keyboardTemplate();
QString keyLabel(int usage);
int inputCode(int usage);
// Geometry only: cannot add LEDs, change their IDs, or change capabilities.
bool applyLayout(DeviceDescription& description, const QString& fileName = { });

class DiscoveryCache
{
public:
    using Scan = std::function<QList<DeviceInstance>(const QStringList&)>;
    explicit DiscoveryCache(Scan scan);
    ~DiscoveryCache();
    QList<DeviceInstance> discover(const QStringList& context);
    bool pending() const;

private:
    QThread m_thread;
    QObject* m_worker;
    mutable QMutex m_mutex;
    Scan m_scan;
    QList<DeviceInstance> m_devices;
    QStringList m_context;
    QElapsedTimer m_age;
    bool m_pending = false;
    bool m_uncollected = false;
};

// Sessions live exclusively on the worker. Opening and restoring hardware must
// have bounded operations; regular frame submission never waits for I/O.
class Session
{
public:
    virtual ~Session() = default;
    virtual QString open() = 0;
    virtual QString write(const DeviceFrame& frame) = 0;
    virtual void close() = 0;
    virtual int minimumIntervalMs() const { return 1; }
};
class Backend final : public QObject, public DeviceBackend
{
public:
    using Factory = std::function<std::unique_ptr<Session>()>;
    explicit Backend(Factory factory);
    ~Backend() override;
    void start(const QVariantMap&) override;
    void setEnabled(bool enabled) override;
    void setLedFramesPerSecond(int fps) override;
    bool ready() const override;
    QString status() const override { return m_status; }
    quint64 completedFrames() const override { return m_frames; }
    void submit(const DeviceFrame& frame) override;

private:
    void dispatch();
    QThread m_thread;
    QObject* m_worker;
    Factory m_factory;
    std::unique_ptr<Session> m_session;
    QTimer m_timer;
    QElapsedTimer m_lastWrite;
    std::optional<DeviceFrame> m_pending;
    QString m_status;
    quint64 m_frames = 0;
    bool m_started = false, m_enabled = false, m_busy = false, m_open = false;
    int m_fps = 30, m_minimumInterval = 1;
};
}
