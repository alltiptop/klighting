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
    for (const auto& d : drivers.devices()) {
        QString presetId;
        for (const auto& p : c.presets)
            if (p.keyboardDeviceId == d.instance.description.id) {
                if (presetId.isEmpty() || p.id == c.defaultId)
                    presetId = p.id;
                if (p.id == c.defaultId)
                    break;
            }
        if (!presetId.isEmpty())
            return { { d.driverId, d.instance.id, d.instance.description.id, d.instance.description.name,
                presetId, true } };
    }
    return { };
}
DeviceManager::DeviceManager(const DeviceEnvironment& environment, const QStringList& directories)
    : m_drivers(environment, directories)
{
}
void DeviceManager::configure(const PresetCollection& collection, const QVariantMap& options)
{
    m_collection = collection;
    if (!m_collection.devices && m_legacyDevices)
        m_collection.devices = m_legacyDevices;
    if (m_options != options)
        m_outputs.clear();
    m_options = options;
    reconcile();
    selectPresets(m_windowClass, m_modifiers);
}
bool DeviceManager::refresh()
{
    m_drivers.refresh();
    const bool changed = reconcile();
    if (changed)
        selectPresets(m_windowClass, m_modifiers);
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
        if (found != m_outputs.end() && found->second.instance.options == device->instance.options) {
            found->second.configuration = config;
            found->second.backend->setEnabled(m_enabled);
            continue;
        }
        if (found != m_outputs.end())
            m_outputs.erase(found);
        auto backend = device->driver->createBackend(device->instance);
        if (!backend)
            continue;
        backend->setEnabled(m_enabled);
        backend->setLedFramesPerSecond(m_fps);
        auto options = device->instance.options;
        // Legacy ASUS override is scoped to that driver, never sent to other modules.
        if (device->driverId == "asus-g615lw")
            for (auto it = m_options.cbegin(); it != m_options.cend(); ++it)
                options.insert(it.key(), it.value());
        backend->start(options);
        m_outputs.emplace(
            config.identity(), Output { config, device->instance, std::move(backend), { }, { }, { }, { } });
        changed = true;
    }
    return changed;
}
void DeviceManager::selectPresets(const QString& windowClass, const ModifierState& modifiers)
{
    const bool windowChanged = m_windowClass != windowClass;
    m_windowClass = windowClass;
    m_modifiers = modifiers;
    if (windowChanged)
        reconcile();
    for (auto& [id, output] : m_outputs) {
        const int index = devicePreset(m_collection, output.configuration, windowClass);
        if (index < 0)
            continue;
        if (output.preset.id != m_collection.presets[index].id)
            output.lastPalette.clear();
        output.preset = m_collection.presets[index];
        output.styles = deviceKeyStyles(output.preset, output.instance.description, modifiers);
        output.effects = deviceEffects(output.preset, output.instance.description, modifiers);
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
    }
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
QSet<QString> DeviceManager::ambientEffects() const
{
    QSet<QString> result;
    for (const auto& [id, output] : m_outputs)
        if (!powerPaused(output.configuration))
            result.unite(
                activeEffects(output.preset, output.styles, output.instance.description, output.effects));
    return result;
}
QSet<QString> DeviceManager::notificationEffects() const { return ambientEffects(); }
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
                initial.configure(runtimeEffects(m_collection),
                    activeEffects(output.preset, output.styles, output.instance.description, output.effects));
                output.lastPalette = initial.colors();
                for (auto it = palette.cbegin(); it != palette.cend(); ++it)
                    output.lastPalette.insert(it.key(), it.value());
            }
            auto frame = lightingColors(output.preset, output.styles, output.instance.description,
                paused ? output.lastPalette : palette, output.effects,
                paused ? QMap<QString, QColor>() : pulses, brightness);
            if (!output.instance.description.capabilities.testFlag(DeviceCapability::Color)) {
                const auto& channel = output.configuration.brightnessChannel;
                const auto grayscale = [&](const QColor& color) {
                    const int value = channel == "red" ? color.red()
                        : channel == "green"           ? color.green()
                        : channel == "blue"            ? color.blue()
                                                       : (color.red() + color.green() + color.blue() + 1) / 3;
                    return QColor(value, value, value);
                };
                frame.uniform = grayscale(frame.uniform);
                for (auto& color : frame.keys)
                    color = grayscale(color);
                for (auto& strip : frame.strips)
                    for (auto& color : strip)
                        color = grayscale(color);
            }
            output.backend->submit(frame);
        }
}
QString DeviceManager::status() const
{
    QStringList status = m_conflicts;
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
