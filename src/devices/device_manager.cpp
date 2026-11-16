// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_manager.h"
#include <algorithm>

namespace Ambient
{
QString conflictingDevice(const DeviceConfiguration& device, const QList<DeviceConfiguration>& configurations,
    const LoadedDriver& drivers)
{
    const auto* candidate = drivers.find(device.identity());
    if (!candidate)
        return { };
    for (const auto& other : configurations) {
        const auto* found = drivers.find(other.identity());
        if (other.enabled && found && devicesConflict(*candidate, *found))
            return other.name;
    }
    return { };
}
QList<DeviceConfiguration> configuredDevices(const PresetCollection& c, const LoadedDriver& drivers)
{
    if (c.devices)
        return *c.devices;
    // Existing settings controlled one device. Do not silently add new hardware.
    for (const auto& d : drivers.devices())
        for (const auto& p : c.presets)
            if (profileFor(p, { d.driverId, d.instance.id, d.instance.description.id }))
                return { { d.driverId, d.instance.id, d.instance.description.id, d.instance.description.name,
                    true } };
    return { };
}
DeviceManager::DeviceManager(const DeviceEnvironment& environment, const QStringList& directories)
    : m_drivers(environment, directories)
{
}
void DeviceManager::configure(const PresetCollection& collection)
{
    m_collection = collection;
    if (!m_collection.devices && m_legacyDevices)
        m_collection.devices = m_legacyDevices;
    for (auto& [id, output] : m_outputs)
        output.presetIndex = -1; // IDs/indices alone cannot identify an edited document.
    m_drivers.refresh(); // Apply newly imported/edited driver options before reconciling backends.
    reconcile();
    selectPresets(m_windowClass, m_modifiers);
}
bool DeviceManager::refresh()
{
    m_drivers.refresh();
    const bool reconciled = reconcile();
    const bool changed = updateReadiness() || reconciled;
    if (changed)
        selectPresets(m_windowClass, m_modifiers);
    return changed;
}
bool DeviceManager::updateReadiness()
{
    bool changed = false;
    for (auto& [id, output] : m_outputs) {
        const bool ready = output.backend->ready();
        changed |= output.observedReady != ready;
        output.observedReady = ready;
    }
    return changed;
}
bool DeviceManager::reconcile()
{
    // Remember the automatic legacy choice across disconnects and reconfiguration.
    // Do not transfer control to a different same-model device when it disappears.
    if (!m_collection.devices) {
        const auto legacy = configuredDevices(m_collection, m_drivers);
        if (!legacy.isEmpty())
            m_collection.devices = m_legacyDevices = legacy;
    }
    QSet<QString> wanted;
    bool changed = false;
    const auto configured = configuredDevices(m_collection, m_drivers);
    m_conflicts.clear();
    for (const auto& config : configured) {
        const auto* device = m_drivers.find(config.identity());
        if (!config.enabled || !device || device->instance.description.id != config.modelId
            || devicePreset(m_collection, config, m_windowClass) < 0)
            continue;
        const auto conflict = conflictingDevice(config, configured, m_drivers);
        if (!conflict.isEmpty()) {
            m_conflicts.append(
                QStringLiteral("%1 conflicts with %2; disable one device").arg(config.name, conflict));
            continue;
        }
        wanted.insert(config.identity());
    }
    // Stop previous owners before opening any replacement, regardless of list order.
    for (auto it = m_outputs.begin(); it != m_outputs.end();)
        if (!wanted.contains(it->first)) {
            it = m_outputs.erase(it);
            changed = true;
        } else
            ++it;
    for (const auto& config : configured) {
        if (!wanted.contains(config.identity()))
            continue;
        const auto* device = m_drivers.find(config.identity());
        auto found = m_outputs.find(config.identity());
        if (found != m_outputs.end() && found->second.instance.options == device->instance.options
            && found->second.instance.description == device->instance.description) {
            found->second.configuration = config;
            found->second.backend->setEnabled(m_enabled);
            continue;
        }
        if (found != m_outputs.end())
            m_outputs.erase(found);
        auto backend = device->driver->createBackend(device->instance);
        if (!backend)
            continue;
        backend->readinessChanged = [this] {
            if (readinessChanged)
                readinessChanged();
        };
        backend->setEnabled(m_enabled);
        backend->setLedFramesPerSecond(m_fps);
        backend->start(device->instance.options);
        const bool ready = backend->ready();
        m_outputs.emplace(config.identity(),
            Output { config, device->instance, std::move(backend), { }, { }, { }, { }, ready });
        changed = true;
    }
    return changed;
}
bool DeviceManager::selectPresets(const QString& windowClass, const ModifierState& modifiers)
{
    const bool windowChanged = m_windowClass != windowClass;
    m_windowClass = windowClass;
    m_modifiers = modifiers;
    bool changed = windowChanged && reconcile();
    for (auto& [id, output] : m_outputs) {
        const int index = devicePreset(m_collection, output.configuration, windowClass);
        if (index < 0)
            continue;
        const int layer = activePresetLayer(m_collection.presets[index], modifiers);
        if (output.presetIndex == index && output.layer == layer)
            continue;
        changed = true;
        ++m_profileResolutions;
        const auto* profile = profileFor(m_collection.presets[index], output.configuration);
        if (output.preset.id != profile->id)
            output.lastPalette.clear();
        output.preset = *profile;
        output.presetIndex = index;
        output.layer = layer;
        output.styles = deviceKeyStylesForLayer(output.preset, output.instance.description, layer);
        output.effects
            = deviceEffectsForLayer(output.preset, output.instance.description, layer, output.styles);
        if (lightbarOff(output.configuration)) {
            auto off = [&](int id) {
                output.styles.insert(id, { KeyMode::Off, Qt::black });
                output.effects.insert(id, { });
            };
            off(LightbarId);
            for (const auto& strip : output.instance.description.strips) {
                off(strip.id);
                for (const auto& element : strip.elements)
                    off(element.id);
            }
        }
        output.usedEffects
            = activeEffects(output.preset, output.styles, output.instance.description, output.effects);
    }
    return changed;
}
void DeviceManager::setEnabled(bool enabled)
{
    m_enabled = enabled;
    for (auto& [id, output] : m_outputs)
        output.backend->setEnabled(enabled);
}
bool DeviceManager::powerPaused(const DeviceConfiguration& device) const
{
    return batteryPolicyActive(m_power, device.pauseOnBattery, device.pauseOnLowBattery);
}
bool DeviceManager::lightbarOff(const DeviceConfiguration& device) const
{
    return batteryPolicyActive(m_power, device.lightbarOffOnBattery, device.lightbarOffOnLowBattery);
}
void DeviceManager::setPowerState(const PowerState& state)
{
    if (m_power == state)
        return;
    m_power = state;
    for (auto& [id, output] : m_outputs)
        output.presetIndex = -1;
    selectPresets(m_windowClass, m_modifiers);
}
void DeviceManager::setLedFramesPerSecond(int fps)
{
    m_fps = fps;
    for (auto& [id, output] : m_outputs)
        output.backend->setLedFramesPerSecond(fps);
}
bool DeviceManager::ready() const
{
    return m_enabled && std::any_of(m_outputs.begin(), m_outputs.end(), [](const auto& entry) {
        return entry.second.backend->ready();
    });
}
QSet<QString> DeviceManager::requiredEffects() const
{
    QSet<QString> result;
    for (const auto& [id, output] : m_outputs)
        if (!powerPaused(output.configuration))
            result.unite(output.usedEffects);
    return result;
}
void DeviceManager::submit(const QMap<QString, QColor>& palette, const QMap<QString, QColor>& pulses,
    const QMap<QString, int>& brightness)
{
    if (!m_enabled)
        return;
    for (auto& [id, output] : m_outputs)
        if (output.backend->ready()) {
            const bool paused = powerPaused(output.configuration);
            if (!paused)
                output.lastPalette = palette;
            else if (output.lastPalette.isEmpty()) {
                // Startup on battery still uses each Ambient effect's dark floor.
                EffectPalette initial;
                initial.configure(runtimeEffects(m_collection), output.usedEffects);
                output.lastPalette = initial.colors();
                for (auto it = palette.cbegin(); it != palette.cend(); ++it)
                    output.lastPalette.insert(it.key(), it.value());
            }
            ++m_compositions;
            auto frame = lightingColors(output.preset, output.styles, output.instance.description,
                paused ? output.lastPalette : palette, output.effects,
                paused ? QMap<QString, QColor>() : pulses, brightness);
            const auto adapt = [&](const QColor& color) {
                return deviceColor(color, output.instance, output.configuration.brightnessChannel);
            };
            frame.uniform = adapt(frame.uniform);
            for (auto& color : frame.keys)
                color = adapt(color);
            for (auto& strip : frame.strips)
                for (auto& color : strip)
                    color = adapt(color);
            output.backend->submit(frame);
        }
}
QString DeviceManager::status() const
{
    QStringList status = m_conflicts;
    for (const auto& config : configuredDevices(m_collection, m_drivers))
        if (config.enabled && m_drivers.find(config.identity())
            && devicePreset(m_collection, config, m_windowClass) < 0)
            status.append(QStringLiteral(
                "%1: no active preset; add this device to the default preset or an application preset")
                    .arg(config.name));
    for (const auto& [id, output] : m_outputs)
        status.append(id + " [" + output.preset.name + "]: "
            + (powerPaused(output.configuration)
                    ? QStringLiteral("Paused on %1").arg(m_power.lowBattery ? "low battery" : "battery")
                    : output.backend->status()));
    if (!m_drivers.error().isEmpty())
        status.append(m_drivers.error());
    return status.isEmpty() ? QStringLiteral("No enabled devices") : status.join(QStringLiteral("; "));
}
quint64 DeviceManager::completedFrames() const
{
    quint64 frames = 0;
    for (const auto& [id, output] : m_outputs)
        frames += output.backend->completedFrames();
    return frames;
}
}
