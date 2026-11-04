// SPDX-License-Identifier: GPL-3.0-or-later
#include "lighting_runtime.h"
#include "settings.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Ambient
{
LightingRuntime::LightingRuntime(
    const DeviceEnvironment& environment, const QStringList& directories, QObject* parent)
    : QObject(parent)
    , m_devices(environment, directories)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        ++m_animationWakeups;
        tick();
    });
    connect(&m_discovery, &QTimer::timeout, this, &LightingRuntime::refresh);
    m_devices.readinessChanged = [this] {
        if (m_readinessPending)
            return;
        m_readinessPending = true;
        // Backend start/enable can notify synchronously during reconciliation.
        // Deliver after it finishes, and coalesce bursts without losing recovery.
        QMetaObject::invokeMethod(
            this,
            [this] {
                m_readinessPending = false;
                m_devices.updateReadiness();
                Q_EMIT requirementsChanged();
                tick(true);
            },
            Qt::QueuedConnection);
    };
    m_discovery.start(2000);
    m_clock.start();
}
LightingRuntime::~LightingRuntime()
{
    m_devices.readinessChanged = { };
    m_devices.setEnabled(false);
}
void LightingRuntime::configure(const PresetCollection& collection)
{
    advance();
    m_pendingSample.reset();
    m_lastSample.invalidate();
    m_collection = collection;
    m_devices.configure(collection);
    updateEffects();
}
void LightingRuntime::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    advance();
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
    advance();
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
    advance();
    m_windowClass = windowClass;
    m_modifiers = modifiers;
    if (m_devices.selectPresets(windowClass, modifiers))
        updateEffects();
    else
        updateTimer();
}
void LightingRuntime::setPowerState(const PowerState& state)
{
    if (m_power == state)
        return;
    advance();
    m_power = state;
    m_devices.setPowerState(state);
    updateEffects();
}
void LightingRuntime::setCaptureAvailable(bool available)
{
    if (m_captureAvailable == available)
        return;
    advance();
    m_captureAvailable = available;
    if (available)
        m_palette.resumeSampling();
    m_lastSample.invalidate();
    m_picker.reset();
    // An unavailable screen source holds its last color (or the configured dark floor).
    // Other assignments and notifications continue to compose normally.
    updateTimer();
    tick();
}
void LightingRuntime::sample(QByteArrayView rgba, double brightness, double saturation)
{
    const int delay = inputDelayMs();
    if (delay < 0 || rgba.size() != 32 * 18 * 4)
        return;
    advance();
    if (delay > 0)
        m_pendingSample = PendingSample { QByteArray(rgba.data(), rgba.size()), brightness, saturation };
    else {
        m_pendingSample.reset();
        acceptSample(rgba, brightness, saturation);
    }
    updateTimer();
}
int LightingRuntime::inputDelayMs() const
{
    const int fps = captureFps();
    if (!m_captureAvailable || fps == 0)
        return -1;
    return m_lastSample.isValid() ? int(std::max<qint64>(0, (1000 + fps - 1) / fps - m_lastSample.elapsed()))
                                  : 0;
}
void LightingRuntime::acceptSample(QByteArrayView rgba, double brightness, double saturation)
{
    m_lastSample.start();
    m_screenColor = m_picker.select(rgba, brightness, saturation);
    m_palette.sample(m_screenColor);
    ++m_samples;
}
void LightingRuntime::notify(const NotificationEvent& event)
{
    if (!running())
        return;
    advance();
    if (m_notifications.trigger(event))
        tick(true);
    else
        updateTimer();
}
int LightingRuntime::captureFps() const
{
    return running() && m_devices.ready() ? m_palette.samplingFps() : 0;
}
bool LightingRuntime::notificationsEnabled() const { return running() && !m_notifications.empty(); }
bool LightingRuntime::microphoneEnabled() const { return running() && !m_microphoneEffects.isEmpty(); }
void LightingRuntime::setMicrophoneState(const MicrophoneState& state)
{
    const auto next = microphoneEnabled() ? state : MicrophoneState();
    if (m_microphoneState == next)
        return;
    m_microphoneState = next;
    // Mute changes do not wait for a notification or screen-output deadline.
    tick();
}
void LightingRuntime::refresh()
{
    advance();
    if (m_devices.refresh())
        updateEffects();
    else {
        Q_EMIT requirementsChanged();
        updateTimer();
    }
}
void LightingRuntime::updateEffects()
{
    ++m_effectUpdates;
    m_devices.updateReadiness();
    const auto effects = runtimeEffects(m_collection);
    const auto used = m_devices.requiredEffects();
    m_palette.configure(effects, used, m_power);
    // A settled screen may never paint again. New layers and edited Ambient
    // options must reuse its last accepted color without restarting capture.
    if (running() && m_captureAvailable && m_screenColor.isValid())
        m_palette.sample(m_screenColor);
    m_notifications.configure(effects, used, m_power);
    m_microphoneEffects = activeMicrophoneEffects(effects, used, m_power);
    if (!microphoneEnabled())
        m_microphoneState = { };
    Q_EMIT requirementsChanged();
    tick(true);
}
void LightingRuntime::updateTimer()
{
    const int inputDelay = inputDelayMs();
    if (inputDelay < 0) {
        m_pendingSample.reset();
        m_lastSample.invalidate();
        m_screenColor = QColor();
    }
    const int fps = m_notifications.active() ? std::max(m_palette.ledFps(), 60)
        : m_captureAvailable                 ? m_palette.ledFps()
                                             : 0;
    const int outputFps = fps > 0 ? fps : DefaultLedFramesPerSecond;
    if (m_ledFps != outputFps) {
        m_ledFps = outputFps;
        m_devices.setLedFramesPerSecond(outputFps);
    }
    if (!running()) {
        m_timer.stop();
        return;
    }
    int delay = m_captureAvailable ? m_palette.nextUpdateMs() : -1;
    if (m_pendingSample)
        delay = delay < 0 ? inputDelay : std::min(delay, inputDelay);
    // An expired pulse still needs one final frame to remove its overlay.
    if (m_notifications.active() || m_notifications.colors() != m_lastPulses) {
        const int notificationDelay = int(std::ceil(std::max(0.0, m_nextNotificationOutput - m_time)));
        delay = delay < 0 ? notificationDelay : std::min(delay, notificationDelay);
    }
    if (delay < 0)
        m_timer.stop();
    else if (!m_timer.isActive() || m_timer.remainingTime() > delay)
        m_timer.start(delay);
}
void LightingRuntime::advance()
{
    const double now = m_clock.nsecsElapsed() / 1000000.0;
    const double elapsed = now - m_time;
    m_time = now;
    if (!running())
        return;
    if (m_captureAvailable)
        m_palette.advance(elapsed, false);
    m_notifications.advance(elapsed);
}
void LightingRuntime::tick(bool force)
{
    ++m_ticks;
    advance();
    if (running()) {
        if (m_captureAvailable) {
            if (m_pendingSample && inputDelayMs() == 0) {
                const auto pending = std::exchange(m_pendingSample, std::nullopt);
                acceptSample(pending->rgba, pending->brightness, pending->saturation);
            }
            m_palette.advance(0);
        }
        const auto palette = m_palette.colors();
        const auto brightness = m_palette.brightnessFilters();
        const bool notificationDue = force || m_time >= m_nextNotificationOutput;
        const auto pulses = notificationDue ? m_notifications.colors() : m_lastPulses;
        const auto microphone = microphoneColors(m_microphoneEffects, m_microphoneState);
        if (force || palette != m_lastPalette || pulses != m_lastPulses || brightness != m_lastBrightness
            || microphone != m_lastMicrophoneColors) {
            auto overlays = pulses;
            overlays.insert(microphone);
            m_devices.submit(palette, overlays, brightness);
            m_lastPalette = palette;
            m_lastPulses = pulses;
            m_lastMicrophoneColors = microphone;
            m_lastBrightness = brightness;
        }
        if (notificationDue)
            m_nextNotificationOutput = m_time + ledIntervalMs(60);
    }
    updateTimer();
}
QVariantMap LightingRuntime::status() const
{
    return { { "running", running() }, { "locked", m_locked }, { "devices", m_devices.count() },
        { "driverStatus", m_devices.status() }, { "captureAvailable", m_captureAvailable },
        { "captureFps", captureFps() }, { "samples", m_samples }, { "sent", m_devices.completedFrames() },
        { "windowClass", m_windowClass }, { "animationWakeups", m_animationWakeups }, { "ticks", m_ticks },
        { "effectUpdates", m_effectUpdates }, { "compositions", m_devices.compositions() },
        { "profileResolutions", m_devices.profileResolutions() },
        { "microphoneEnabled", microphoneEnabled() } };
}
}
