// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "led_transport.h"
#include <QByteArray>
#include <QList>
#include <QThread>
#include <functional>
#include <memory>

namespace Ambient
{
QList<QByteArray> lampArrayPackets(const LedFrame& frame, const QList<bool>& keyboardLamps);
struct LampArrayWriteResult {
    QString error;
    bool written = false; // A complete frame reached the final commit report.
};
// Worker-owned output state, independent of USB discovery and interface ownership.
class LampArrayOutput
{
public:
    LampArrayWriteResult write(const LedFrame& frame, const QList<bool>& keyboardLamps,
        const std::function<QString(QByteArray)>& send);
    void reset();

private:
    QList<QRgb> m_lastColors;
};
class LampArrayDevice;

class LampArrayTransport final : public LedTransport
{
public:
    LampArrayTransport();
    ~LampArrayTransport() override;
    void submit(const LedFrame& frame, quint64 operation) override;
    void release(quint64 operation) override;
    void shutdown() override;

private:
    void dispatch(const LedFrame* frame, quint64 operation);
    QThread m_thread;
    QObject* m_worker;
    std::unique_ptr<LampArrayDevice> m_device;
};
}
