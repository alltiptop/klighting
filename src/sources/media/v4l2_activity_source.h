// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sources/media_activity_source.h"
#include <chrono>
#include <functional>
#include <thread>

namespace Ambient
{
// Read-only metadata probes; invalid QVariant means discovery itself failed.
bool isV4l2CaptureDevice(const QByteArray& capabilities);
QVariant openCameraDevices(
    const QSet<quint64>& devices, const QString& procRoot, quint32 userId, std::stop_token stop = { });
QVariant v4l2CameraInUse(std::stop_token stop);

class V4l2ActivitySource final : public MediaActivitySource
{
public:
    using Probe = std::function<QVariant(std::stop_token)>;
    explicit V4l2ActivitySource(
        Probe probe = v4l2CameraInUse, std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    ~V4l2ActivitySource() override;
    void setEnabled(bool enabled) override;

private:
    Probe m_probe;
    std::chrono::milliseconds m_interval;
    std::jthread m_worker;
    QVariant m_value;
    quint64 m_generation = 0;
    bool m_enabled = false;
};
}
