// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "driver_loader.h"
#include "effect_palette.h"
#include "power_state.h"
#include <map>

namespace Ambient
{
QList<DeviceConfiguration> configuredDevices(const PresetCollection& collection, const LoadedDriver& drivers);
QString conflictingDevice(const DeviceConfiguration& device, const QList<DeviceConfiguration>& configurations,
    const LoadedDriver& drivers);
class DeviceManager
{
public:
    explicit DeviceManager(const DeviceEnvironment& environment = deviceEnvironment(),
        const QStringList& directories = driverDirectories());
    std::function<void()> readinessChanged;
    void configure(const PresetCollection& collection);
    bool refresh();
    bool updateReadiness();
    // Always remembers input; true means resolved output or the backend set changed.
    bool selectPresets(const QString& windowClass, const ModifierState& modifiers);
    void setEnabled(bool enabled);
    void setPowerState(const PowerState& state);
    void setLedFramesPerSecond(int fps);
    bool ready() const;
    QSet<QString> requiredEffects() const;
    void submit(const QMap<QString, QColor>& palette, const QMap<QString, QColor>& pulses,
        const QMap<QString, int>& brightness = { });
    QString status() const;
    quint64 completedFrames() const;
    quint64 compositions() const { return m_compositions; }
    quint64 profileResolutions() const { return m_profileResolutions; }
    qsizetype count() const { return m_outputs.size(); }
    const LoadedDriver& drivers() const { return m_drivers; }

private:
    struct Output {
        DeviceConfiguration configuration;
        DeviceInstance instance;
        std::unique_ptr<DeviceBackend> backend;
        DeviceProfile preset;
        KeyboardStyles styles;
        EffectAssignments effects;
        QMap<QString, QColor> lastPalette;
        bool observedReady = false;
        QSet<QString> usedEffects = { };
        int presetIndex = -1, layer = -1;
    };
    bool reconcile();
    bool powerPaused(const DeviceConfiguration& device) const;
    bool lightbarOff(const DeviceConfiguration& device) const;
    PowerState m_power;
    LoadedDriver m_drivers;
    PresetCollection m_collection;
    std::optional<QList<DeviceConfiguration>> m_legacyDevices;
    std::map<QString, Output> m_outputs;
    QString m_windowClass;
    QStringList m_conflicts;
    ModifierState m_modifiers;
    bool m_enabled = false;
    int m_fps = DefaultLedFramesPerSecond;
    quint64 m_compositions = 0, m_profileResolutions = 0;
};
}
