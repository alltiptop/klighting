// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lighting_runtime.h"
#include "sources/integration_sources.h"
#include "sources/notification_source.h"
#include "sources/power_source.h"
#include "sources/system_trigger_source.h"

namespace Ambient
{
class LightingService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.klighting.Service1")
public:
    explicit LightingService(const DeviceEnvironment& environment = deviceEnvironment(),
        const QStringList& directories = driverDirectories(), IntegrationSources sources = { },
        QObject* parent = nullptr);
public Q_SLOTS:
    Q_SCRIPTABLE QString Reload();
    Q_SCRIPTABLE QString RetryDesktopIntegration();
    Q_SCRIPTABLE QVariantMap Status() const;

private:
    void updateRequirements();
    void updateControl();
    void updateDesktop();
    void updateCapture();
    LightingRuntime m_runtime;
    PowerSource m_power;
    NotificationSource m_notifications;
    SystemTriggerSource m_systemTriggers;
    IntegrationSources m_sources;
    QVariantMap m_captureOptions, m_audioValues, m_mediaValues;
    QString m_error;
    double m_brightness = 1, m_saturation = 1;
    bool m_configured = false, m_desktopKnown = false;
};
}
