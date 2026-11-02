// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_driver.h"

namespace Ambient
{
class AsusControls final : public QObject, public DeviceControls
{
    Q_OBJECT
public:
    AsusControls();
    void readLightbar(const QVariantMap& options, std::function<void(LightbarState)> done) override;
    void writeLightbar(bool enabled, std::function<void(QString)> done) override;

private:
    QString m_owner, m_path;
    quint64 m_generation = 0;
};
}
