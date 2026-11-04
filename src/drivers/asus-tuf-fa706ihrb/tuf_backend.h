// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_driver.h"
#include <QDBusConnection>
#include <optional>

namespace Ambient
{
namespace Tuf
{
    inline constexpr auto Service = "org.asuslinux.Daemon";
    inline constexpr auto Path = "/org/asuslinux/tuf";
    inline constexpr auto Interface = "org.asuslinux.Aura";

    struct State {
        QColor savedColor;
        uint brightness = 0;
        bool keyboardAwake = false;
    };

    QByteArray rawPacket(const QColor& color);
    // Decode whole-keyboard Static state; reject unsupported property encodings.
    std::optional<State> decodeState(const QVariantMap& properties, QString* error = nullptr);
}

class TufWorker;
class TufBackend : public DeviceBackend
{
public:
    explicit TufBackend(const QDBusConnection& bus = QDBusConnection::systemBus(),
        QString service = QString::fromLatin1(Tuf::Service), QString path = QString::fromLatin1(Tuf::Path));
    // Restores the saved Static color before returning, without an event loop. Blocks for
    // up to 2 s when asusd does not answer.
    ~TufBackend() override;
    TufBackend(const TufBackend&) = delete;
    TufBackend& operator=(const TufBackend&) = delete;

    void start(const QVariantMap& options) override;
    void setEnabled(bool enabled) override;
    void setLedFramesPerSecond(int fps) override;
    bool ready() const override;
    QString status() const override;
    quint64 completedFrames() const override;
    void submit(const DeviceFrame& frame) override;

private:
    TufWorker* m_worker;
};
}
