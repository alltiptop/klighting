// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "desktop_source.h"
#include "kwin_capture_source.h"
#include "lighting_runtime.h"
#include "notification_source.h"
#include "power_source.h"
#include <KSharedConfig>

namespace Ambient
{
class LightingService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.klighting.Service1")
public:
    explicit LightingService(const DeviceEnvironment& environment = deviceEnvironment(),
        const QStringList& directories = driverDirectories(),
        std::unique_ptr<DesktopSource> desktop = nullptr, QObject* parent = nullptr);
public Q_SLOTS:
    Q_SCRIPTABLE QString Reload();
    Q_SCRIPTABLE QVariantMap Status() const;

private:
    void updateRequirements();
    void updateControl();
    LightingRuntime m_runtime;
    PowerSource m_power;
    NotificationSource m_notifications;
    std::unique_ptr<DesktopSource> m_desktop;
    KWinCaptureSource m_kwin;
    KSharedConfig::Ptr m_config;
    QVariantMap m_captureOptions;
    QString m_error;
    double m_brightness = 1, m_saturation = 1;
    bool m_configured = false, m_desktopKnown = false;
};
}
