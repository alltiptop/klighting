// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sources/media_activity_source.h"
#include <QTimer>

namespace Ambient
{
class PipeWireActivitySource final : public MediaActivitySource
{
public:
    PipeWireActivitySource();
    ~PipeWireActivitySource() override;
    void setEnabled(bool enabled) override;

private:
    struct Connection;
    void start();
    void deliver(quint64 generation, const QVariantMap& values, bool failed);
    std::unique_ptr<Connection> m_connection;
    QTimer m_retry, m_timeout;
    QVariantMap m_values;
    quint64 m_generation = 0;
    bool m_enabled = false;
};
}
