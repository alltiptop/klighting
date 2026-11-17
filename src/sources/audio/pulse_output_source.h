// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sources/audio_state_source.h"
#include <QTimer>
#include <memory>

namespace Ambient
{
class PulseOutputSource : public AudioStateSource
{
public:
    PulseOutputSource();
    ~PulseOutputSource() override;
    void setEnabled(bool enabled) override;

private:
    struct Connection;
    void start();
    void deliver(quint64 generation, const QVariantMap& values, bool failed = false);
    std::unique_ptr<Connection> m_connection;
    QTimer m_retry;
    quint64 m_generation = 0;
    bool m_enabled = false;
};
}
