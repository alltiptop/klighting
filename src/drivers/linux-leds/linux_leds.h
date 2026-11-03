// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "driver_support.h"
#include <QLockFile>

namespace Ambient::LinuxLeds
{
QList<DeviceInstance> discover(const QString& root, const QString& boardName);
class Session final : public Generic::Session
{
public:
    explicit Session(QString path);
    ~Session() override;
    QString open() override;
    QString write(const DeviceFrame& frame) override;
    void close() override;

private:
    QString put(const QString& attribute, const QByteArray& value);
    QString m_path;
    std::unique_ptr<QLockFile> m_lock;
    QStringList m_channels;
    QList<int> m_limits;
    int m_maximum = 0, m_base = 0, m_lastBrightness = -1;
    QByteArray m_originalColor, m_lastColor, m_trigger;
    bool m_touched = false;
};
}
