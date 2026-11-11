// SPDX-License-Identifier: GPL-3.0-or-later
#include "lighting_service.h"
#include "application_settings.h"
#include "component_info.h"
#include "lighting_ipc.h"
#include <QFile>
#include <algorithm>
#include <linux/input-event-codes.h>

namespace Ambient
{
LightingService::LightingService(const DeviceEnvironment& environment, const QStringList& directories,
    IntegrationSources sources, QObject* parent)
    : QObject(parent)
    , m_runtime(environment, directories)
    , m_sources(std::move(sources))
{
    connect(&m_runtime, &LightingRuntime::requirementsChanged, this, &LightingService::updateRequirements);
    if (m_sources.hardwareGuard)
        connect(m_sources.hardwareGuard.get(), &HardwareControlGuard::changed, this,
            &LightingService::updateControl);
    if (m_sources.desktop)
        connect(m_sources.desktop.get(), &DesktopSource::changed, this, &LightingService::updateDesktop);
    if (m_sources.capture) {
        connect(m_sources.capture.get(), &CaptureSource::changed, this, &LightingService::updateCapture);
        connect(m_sources.capture.get(), &CaptureSource::sample, this,
            [this](const QByteArray& rgba) { m_runtime.sample(rgba, m_brightness, m_saturation); });
    }
    if (m_sources.microphone)
        connect(m_sources.microphone.get(), &MicrophoneSource::changed, this,
            [this] { m_runtime.setMicrophoneState(m_sources.microphone->state()); });
    connect(&m_power, &PowerSource::stateChanged, this, [this] { m_runtime.setPowerState(m_power.state()); });
    connect(&m_power, &PowerSource::powerConnected, this,
        [this] { m_runtime.notify({ { }, { }, { }, { }, 1, QStringLiteral("power-connected") }); });
    connect(&m_notifications, &NotificationSource::received, &m_runtime, &LightingRuntime::notify);
    updateDesktop();
    m_runtime.setCaptureAvailable(m_sources.capture && m_sources.capture->state().ready);
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
    const auto settings = loadApplicationSettings(SettingsAccess::Migrate, &m_error);
    if (!settings || !m_runtime.drivers().migrateSettings(&m_error))
        return m_error; // No partial runtime reconfiguration after a failed migration.
    m_captureOptions = settings->capture;
    m_brightness = settings->brightness;
    m_saturation = settings->saturation;
    m_configured = true;
    QSet<quint32> tracked { KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTALT,
        KEY_RIGHTALT, KEY_LEFTMETA, KEY_RIGHTMETA, KEY_COMPOSE, KEY_MENU, KEY_FN, KEY_CAPSLOCK };
    for (const auto& preset : collection->presets)
        for (const auto& conditions : presetLayerConditions(preset))
            for (const auto& condition : conditions)
                if (condition.startsWith("key:"))
                    tracked.insert(condition.mid(4).toUInt());
    if (m_sources.desktop)
        m_sources.desktop->setTrackedKeys(tracked);
    m_runtime.configure(*collection);
    updateControl();
    updateRequirements();
    return { };
}
void LightingService::updateControl()
{
    // Resolve the initial desktop/lock snapshot before the first hardware frame.
    m_runtime.setEnabled(
        m_configured && m_desktopKnown && (!m_sources.hardwareGuard || m_sources.hardwareGuard->allowed()));
}
void LightingService::updateDesktop()
{
    const auto state = m_sources.desktop ? m_sources.desktop->state()
                                         : DesktopState { ProviderState::Unavailable, { }, { }, { } };
    if (state.provider == ProviderState::Available) {
        if (state.locked.has_value()) {
            m_runtime.setLocked(*state.locked);
            m_desktopKnown = true;
        }
        m_runtime.setDesktop(
            state.windowClass.value_or(QString()), state.modifiers.value_or(ModifierState()));
    } else {
        // Lose application/held-key overrides without turning unknown lock state into unlocked.
        m_runtime.setDesktop({ }, { });
        if (state.provider == ProviderState::Unavailable)
            m_desktopKnown = true; // Standalone operation with no desktop provider.
    }
    updateControl();
}
void LightingService::updateCapture()
{
    m_runtime.setCaptureAvailable(m_sources.capture && m_sources.capture->state().ready);
    updateRequirements();
}
void LightingService::updateRequirements()
{
    if (!m_configured && !m_runtime.drivers().devices().isEmpty()) {
        Reload();
        return;
    }
    m_notifications.setEnabled(m_runtime.notificationsEnabled());
    if (m_sources.microphone)
        m_sources.microphone->setEnabled(m_runtime.microphoneEnabled());
    if (m_sources.capture)
        m_sources.capture->requestCapture(m_runtime.captureFps(), m_captureOptions);
}
QString LightingService::RetryDesktopIntegration()
{
    return m_sources.desktop ? m_sources.desktop->retry() : QStringLiteral("No desktop provider");
}
QVariantMap LightingService::Status() const
{
    auto status = m_runtime.status();
    status.insert(componentInfo(Component::Service));
    const auto desktop = m_sources.desktop ? m_sources.desktop->state()
                                           : DesktopState { ProviderState::Unavailable, { }, { }, { } };
    const auto capture = m_sources.capture ? m_sources.capture->state() : CaptureState();
    const auto microphone = m_sources.microphone ? m_sources.microphone->state() : MicrophoneState();
    status.insert("microphoneBackend", microphone.backend);
    status.insert("microphoneAvailable", microphone.available);
    status.insert("microphoneMuteKnown", microphone.muteKnown);
    status.insert("microphoneMuted", microphone.muted);
    status.insert("microphoneSourceName", microphone.sourceName);
    status.insert("microphoneDescription", microphone.description);
    status.insert("microphoneReason",
        m_sources.microphone ? microphone.reason : QStringLiteral("No microphone observer"));
    status.insert("desktopInfo", m_sources.desktop ? m_sources.desktop->info() : QVariantMap());
    status.insert("captureInfo", capture.info);
    status.insert("desktopAvailable", desktop.provider == ProviderState::Available);
    status.insert("windowAvailable", desktop.windowClass.has_value());
    status.insert("keysAvailable", desktop.modifiers.has_value());
    status.insert("lockAvailable", desktop.locked.has_value());
    status.insert("captureProviderAvailable", capture.providerAvailable);
    status.insert("screenAvailable", capture.screenAvailable);
    status.insert("captureEnabled", capture.enabled);
    status.insert("error", m_error);
    status.insert(
        "desktop", m_sources.desktop ? m_sources.desktop->status() : QStringLiteral("No desktop provider"));
    status.insert("desktopCanRetry", m_sources.desktop && m_sources.desktop->canRetry());
    status.insert("capture", capture.status);
    status.insert("safeToControl", !m_sources.hardwareGuard || m_sources.hardwareGuard->allowed());
    status.insert("controlStatus", m_sources.hardwareGuard ? m_sources.hardwareGuard->status() : QString());
    status.insert("notifications", m_notifications.status());
    status.insert("power", m_power.status());
    return status;
}
}
