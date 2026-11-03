// SPDX-License-Identifier: GPL-3.0-or-later
#include "lighting_runtime.h"
#include "settings.h"
#include <algorithm>

namespace Ambient
{
LightingRuntime::LightingRuntime(
    const DeviceEnvironment& environment, const QStringList& directories, QObject* parent)
    : QObject(parent)
    , m_devices(environment, directories)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &LightingRuntime::tick);
    connect(&m_discovery, &QTimer::timeout, this, &LightingRuntime::refresh);
    m_discovery.start(2000);
    m_elapsed.start();
}
LightingRuntime::~LightingRuntime() { m_devices.setEnabled(false); }
void LightingRuntime::configure(const PresetCollection& collection, const QVariantMap& options)
{
    m_collection = collection;
    m_devices.configure(collection, options);
    updateEffects();
}
void LightingRuntime::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_devices.setEnabled(running());
    if (!running())
        m_notifications.reset();
    updateEffects();
}
void LightingRuntime::setLocked(bool locked)
{
    if (m_locked == locked)
        return;
    m_locked = locked;
    m_devices.setEnabled(running());
    m_notifications.reset();
    m_picker.reset();
    updateEffects();
}
void LightingRuntime::setDesktop(const QString& windowClass, const ModifierState& modifiers)
{
    if (m_windowClass == windowClass && m_modifiers == modifiers)
        return;
    m_windowClass = windowClass;
    m_modifiers = modifiers;
    updateEffects();
}
void LightingRuntime::setPowerState(const PowerState& state)
{
    if (m_power == state)
        return;
    m_power = state;
    m_devices.setPowerState(state);
    updateEffects();
}
void LightingRuntime::setCaptureAvailable(bool available)
{
    if (m_captureAvailable == available)
        return;
    m_captureAvailable = available;
    m_lastSample.invalidate();
    m_picker.reset();
    // An unavailable screen source holds its last color (or the configured dark floor).
    // Other assignments and notifications continue to compose normally.
    updateTimer();
    tick();
}
void LightingRuntime::sample(QByteArrayView rgba, double brightness, double saturation)
{
    const int fps = captureFps();
    if (!m_captureAvailable || fps == 0 || rgba.size() != 32 * 18 * 4
        || (m_lastSample.isValid() && m_lastSample.elapsed() < 1000 / fps))
        return;
    m_lastSample.start();
    m_palette.sample(m_picker.select(rgba, brightness, saturation));
    ++m_samples;
    tick();
}
void LightingRuntime::notify(const NotificationEvent& event)
{
    if (!running())
        return;
    // Idle time before this event must not consume the new pulse's duration.
    if (!m_timer.isActive())
        m_elapsed.restart();
    m_notifications.trigger(event);
    tick();
}
int LightingRuntime::captureFps() const
{
    return running() && m_devices.ready() ? m_palette.samplingFps() : 0;
}
bool LightingRuntime::notificationsEnabled() const { return running() && !m_notifications.empty(); }
void LightingRuntime::refresh()
{
    if (m_devices.refresh())
        updateEffects();
    Q_EMIT requirementsChanged();
    tick(); // Also delivers a static frame when an asynchronous backend becomes ready.
}
void LightingRuntime::updateEffects()
{
    m_devices.selectPresets(m_windowClass, m_modifiers);
    const auto effects = runtimeEffects(m_collection);
    m_palette.configure(effects, m_devices.ambientEffects(), m_power);
    m_notifications.configure(effects, m_devices.notificationEffects(), m_power);
    Q_EMIT requirementsChanged();
    tick();
}
void LightingRuntime::updateTimer()
{
    const int fps = m_notifications.active() ? std::max(m_palette.ledFps(), 60)
        : m_captureAvailable                 ? m_palette.ledFps()
                                             : 0;
    const int outputFps = fps > 0 ? fps : DefaultLedFramesPerSecond;
    if (m_ledFps != outputFps) {
        m_ledFps = outputFps;
        m_devices.setLedFramesPerSecond(outputFps);
    }
    if (!running() || fps == 0) {
        m_timer.stop();
    } else if (!m_timer.isActive() || m_timer.interval() != ledIntervalMs(fps)) {
        m_elapsed.start();
        m_timer.start(ledIntervalMs(fps));
    }
}
void LightingRuntime::tick()
{
    const auto elapsed = std::min<qint64>(m_elapsed.restart(), 1000);
    if (running()) {
        if (m_captureAvailable)
            m_palette.advance(elapsed);
        m_notifications.advance(elapsed);
        m_devices.submit(m_palette.colors(), m_notifications.colors(), m_palette.brightnessFilters());
    }
    updateTimer();
}
QVariantMap LightingRuntime::status() const
{
    return { { "running", running() }, { "locked", m_locked }, { "devices", m_devices.count() },
        { "driverStatus", m_devices.status() }, { "captureAvailable", m_captureAvailable },
        { "captureFps", captureFps() }, { "samples", m_samples }, { "sent", m_devices.completedFrames() },
        { "windowClass", m_windowClass } };
}
}
