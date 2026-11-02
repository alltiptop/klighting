// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "driver_loader.h"
#include "effect_palette.h"
#include "power_state.h"
#include <map>

namespace Ambient
{
QList<DeviceConfiguration> configuredDevices(const PresetCollection& collection, const LoadedDriver& drivers);
class DeviceManager
{
public:
    explicit DeviceManager(const DeviceEnvironment& environment = deviceEnvironment(),
        const QStringList& directories = driverDirectories());
    void configure(const PresetCollection& collection, const QVariantMap& options = { });
    bool refresh();
    void selectPresets(const QString& windowClass, const ModifierState& modifiers);
    void setEnabled(bool enabled);
    void setPowerState(const PowerState& state);
    void setLedFramesPerSecond(int fps);
    bool ready() const;
    QSet<QString> ambientEffects() const;
    QSet<QString> notificationEffects() const;
    void submit(const QMap<QString, QColor>& palette, const QMap<QString, QColor>& pulses);
    QString status() const;
    quint64 completedFrames() const;
    qsizetype count() const { return m_outputs.size(); }
    const LoadedDriver& drivers() const { return m_drivers; }

private:
    struct Output {
        DeviceConfiguration configuration;
        DeviceInstance instance;
        std::unique_ptr<DeviceBackend> backend;
        LightingPreset preset;
        KeyboardStyles styles;
        EffectAssignments effects;
        QMap<QString, QColor> lastPalette;
    };
    bool reconcile();
    bool powerPaused(const DeviceConfiguration& device) const;
    bool lightbarOff(const DeviceConfiguration& device) const;
    PowerState m_power;
    LoadedDriver m_drivers;
    PresetCollection m_collection;
    std::optional<QList<DeviceConfiguration>> m_legacyDevices;
    QVariantMap m_options;
    std::map<QString, Output> m_outputs;
    QString m_windowClass;
    ModifierState m_modifiers;
    bool m_enabled = false;
    int m_fps = DefaultLedFramesPerSecond;
};
}
