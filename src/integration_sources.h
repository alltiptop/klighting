// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "capture_source.h"
#include "desktop_source.h"
#include <memory>

namespace Ambient
{
// Optional veto supplied by an integration that may encounter another hardware controller.
// Neither the desktop nor the image-source interface requires such a guard.
class HardwareControlGuard : public QObject
{
    Q_OBJECT
public:
    bool allowed() const { return m_allowed; }
    QString status() const { return m_status; }
    void setState(bool allowed, const QString& status)
    {
        if (m_allowed == allowed && m_status == status)
            return;
        m_allowed = allowed;
        m_status = status;
        Q_EMIT changed();
    }
Q_SIGNALS:
    void changed();

private:
    bool m_allowed = false;
    QString m_status = QStringLiteral("Checking hardware ownership");
};

struct IntegrationSources {
    std::unique_ptr<DesktopSource> desktop;
    std::unique_ptr<CaptureSource> capture;
    std::shared_ptr<HardwareControlGuard> hardwareGuard;
};
}
