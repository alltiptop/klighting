// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "microphone_source.h"
#include <QTimer>
#include <memory>

namespace Ambient
{
class PulseMicrophoneSource final : public MicrophoneSource
{
public:
    PulseMicrophoneSource();
    ~PulseMicrophoneSource() override;
    void setEnabled(bool enabled) override;
    MicrophoneState state() const override { return m_state; }

private:
    struct Connection;
    void start();
    void stop();
    void fail(const QString& reason);
    void publish(MicrophoneState state);
    std::unique_ptr<Connection> m_connection;
    MicrophoneState m_state;
    QTimer m_retry;
    bool m_enabled = false;
    quint64 m_generation = 0;
    int m_retryDelay = 500;
};
}
