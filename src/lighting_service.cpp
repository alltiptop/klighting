// SPDX-License-Identifier: GPL-3.0-or-later
#include "lighting_service.h"
#include "kwin_desktop_source.h"
#include "lighting_ipc.h"
#include <KConfigGroup>
#include <QFile>
#include <algorithm>
#include <linux/input-event-codes.h>

namespace Ambient
{
LightingService::LightingService(const DeviceEnvironment& environment, const QStringList& directories,
    std::unique_ptr<DesktopSource> desktop, QObject* parent)
    : QObject(parent)
    , m_runtime(environment, directories)
    , m_desktop(desktop ? std::move(desktop) : std::make_unique<KWinDesktopSource>())
    , m_config(KSharedConfig::openConfig("kwinrc"))
{
    connect(&m_runtime, &LightingRuntime::requirementsChanged, this, &LightingService::updateRequirements);
    connect(&m_kwin, &KWinCaptureSource::changed, this, [this] {
        m_runtime.setCaptureAvailable(m_kwin.captureAvailable());
        updateControl();
        updateRequirements();
    });
    connect(m_desktop.get(), &DesktopSource::stateChanged, this,
        [this](const QString& windowClass, const ModifierState& modifiers, bool locked) {
            m_runtime.setLocked(locked);
            m_runtime.setDesktop(windowClass, modifiers);
            m_desktopKnown = true;
            updateControl();
        });
    connect(m_desktop.get(), &DesktopSource::unavailable, this, [this] {
        // Drop held keys and application overrides. Retain the last known lock state.
        m_runtime.setDesktop({ }, { });
        m_desktopKnown = true;
        updateControl();
    });
    connect(&m_kwin, &KWinCaptureSource::sample, this,
        [this](const QByteArray& rgba) { m_runtime.sample(rgba, m_brightness, m_saturation); });
    connect(&m_power, &PowerSource::stateChanged, this, [this] { m_runtime.setPowerState(m_power.state()); });
    connect(&m_power, &PowerSource::powerConnected, this,
        [this] { m_runtime.notify({ { }, { }, { }, { }, 1, QStringLiteral("power-connected") }); });
    connect(&m_notifications, &NotificationSource::received, &m_runtime, &LightingRuntime::notify);
    Reload();
}
QString LightingService::Reload()
{
    // Generic discovery is asynchronous. A new installation waits for its first device.
    if (!m_configured && m_runtime.drivers().devices().isEmpty()
        && !QFile::exists(presetDirectory() + "/settings-v2.json"))
        return { };
    const auto model = m_runtime.drivers().devices().isEmpty()
        ? QString()
        : m_runtime.drivers().devices().first().instance.description.id;
    const auto collection = loadPresets(defaultCollection(model), &m_error);
    if (!collection)
        return m_error; // Preserve the last valid running collection.
    m_config->reparseConfiguration();
    const KConfigGroup cfg(m_config, ConfigGroup);
    m_captureOptions = { { "screen", cfg.readEntry("ScreenName", QString()) },
        { "bottomOnly", cfg.readEntry("Region", QStringLiteral("full")) == QLatin1String("bottom") },
        { "bottomPercent", std::clamp(cfg.readEntry("BottomPercent", 25), 1, 100) } };
    m_brightness = std::clamp(cfg.readEntry("Brightness", 1.0), 0.0, 1.0);
    m_saturation = std::clamp(cfg.readEntry("Saturation", 1.0), 0.0, 2.0);
    m_configured = true;
    QSet<quint32> tracked { KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTALT,
        KEY_RIGHTALT, KEY_LEFTMETA, KEY_RIGHTMETA, KEY_COMPOSE, KEY_MENU, KEY_FN, KEY_CAPSLOCK };
    for (const auto& preset : collection->presets)
        for (const auto& layer : preset.layers)
            for (const auto& condition : layer.conditions)
                if (condition.startsWith("key:"))
                    tracked.insert(condition.mid(4).toUInt());
    m_desktop->setTrackedKeys(tracked);
    m_runtime.configure(*collection, { { "AuraObjectPath", cfg.readEntry("AuraObjectPath", QString()) } });
    updateControl();
    updateRequirements();
    return { };
}
void LightingService::updateControl()
{
    // Resolve the initial desktop/lock snapshot before the first hardware frame.
    m_runtime.setEnabled(m_configured && m_desktopKnown && m_kwin.safeToControl());
}
void LightingService::updateRequirements()
{
    if (!m_configured && !m_runtime.drivers().devices().isEmpty()) {
        Reload();
        return;
    }
    m_notifications.setEnabled(m_runtime.notificationsEnabled());
    m_kwin.requestCapture(m_runtime.captureFps(), m_captureOptions);
}
QVariantMap LightingService::Status() const
{
    auto status = m_runtime.status();
    status.insert("protocol", Ipc::Version);
    status.insert("error", m_error);
    status.insert("desktop", m_desktop->status());
    status.insert("capture", m_kwin.status());
    status.insert("safeToControl", m_kwin.safeToControl());
    status.insert("notifications", m_notifications.status());
    status.insert("power", m_power.status());
    return status;
}
}
