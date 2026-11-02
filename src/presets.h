// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "keyboard_profile.h"
#include "settings.h"
#include <QJsonObject>
#include <QSet>
#include <optional>

namespace Ambient
{
struct ModifierState {
    QSet<quint32> pressed;
    Qt::KeyboardModifiers effective;
    bool capsLock = false;
    bool operator==(const ModifierState&) const = default;
};
struct StripGradient {
    int strip = FirstStripId, from = 0, to = 0;
    QColor start = Qt::red, end = Qt::blue;
    bool ambient = false;
    QString effectId;
    bool operator==(const StripGradient&) const = default;
};
using StripGradients = QList<StripGradient>;
KeyboardStyles expandStyles(
    const KeyboardStyles& styles, const StripGradients& gradients, const DeviceDescription& device);
inline constexpr int DefaultAssignment = -1;
inline constexpr auto InlineColorId = "@color";
struct EffectAssignment {
    QString id; // Empty uses the keyboard preset's default Ambient effect.
    int opacity = 100;
    QColor color = { }; // Local Solid Color; other entries reference a saved effect.
    bool operator==(const EffectAssignment&) const = default;
};
using EffectList = QList<EffectAssignment>;
using EffectAssignments = QMap<int, EffectList>;
EffectList implicitEffects(const KeyStyle& style);
struct KeyboardLayer {
    QString name = QStringLiteral("Shift");
    QStringList conditions { QStringLiteral("shift") };
    KeyboardStyles styles = inheritedKeyStyles();
    EffectAssignments effects = { };
    StripGradients gradients = { };
    bool operator==(const KeyboardLayer&) const = default;
};
using KeyboardLayers = QList<KeyboardLayer>;
QList<QPair<QString, QString>> availableConditions();
QString conditionName(const QString& id);
bool conditionActive(const QString& condition, const ModifierState& state);
bool layerActive(const KeyboardLayer& layer, const ModifierState& state);
int activeLayer(const KeyboardLayers& layers, const ModifierState& state);
KeyboardStyles resolveLayers(
    const KeyboardStyles& base, const KeyboardLayers& layers, const ModifierState& state);
struct AmbientOptions {
    int fps = DefaultFramesPerSecond, ledFps = DefaultLedFramesPerSecond;
    int smoothing = DefaultSmoothingMs, minimum = DefaultMinimumBrightness;
    int maximum = DefaultMaximumBrightness, temperature = DefaultColorTemperature;
    bool operator==(const AmbientOptions&) const = default;
};
struct NotificationOptions {
    QColor color = QColor("#3daee9");
    QString pattern = QStringLiteral("pulse");
    int period = 1000, cycles = 3, urgency = -1;
    QString application, category, eventId;
    QString trigger = QStringLiteral("notification");
    bool operator==(const NotificationOptions&) const = default;
};
inline constexpr auto DefaultEffectId = "ambient-default";
struct EffectPreset {
    QString id = QStringLiteral("ambient-default"), name = QStringLiteral("Ambient");
    QString type = QStringLiteral("ambient");
    AmbientOptions ambient;
    NotificationOptions notification;
    QColor solidColor = Qt::white;
    bool pauseOnBattery = false, pauseOnLowBattery = false;
    bool operator==(const EffectPreset&) const = default;
};
struct LightingPreset {
    QString id = QStringLiteral("default"), name = QStringLiteral("Default");
    // Key IDs are scoped to the driver description.
    QString keyboardDeviceId = QStringLiteral("asus-g615lw");
    QString defaultEffectId = QStringLiteral("ambient-default");
    KeyboardStyles base;
    KeyboardLayers layers { KeyboardLayer() };
    KeyGroups groups;
    EffectAssignments effects, effectOverrides;
    StripGradients gradients = { };
    QList<EffectPreset> localEffects;
    bool operator==(const LightingPreset&) const = default;
};
KeyboardStyles deviceKeyStyles(
    const LightingPreset& preset, const DeviceDescription& device, const ModifierState& modifiers);
EffectAssignments deviceEffects(
    const LightingPreset& preset, const DeviceDescription& device, const ModifierState& modifiers);
struct ApplicationRule {
    QString name, windowClass, presetId;
    bool enabled = true;
    bool operator==(const ApplicationRule&) const = default;
};
struct DeviceConfiguration {
    QString driverId, instanceId, modelId, name, presetId;
    bool enabled = true, followApplications = true;
    bool pauseOnBattery = false, pauseOnLowBattery = false;
    bool lightbarOffOnBattery = false, lightbarOffOnLowBattery = false;
    QString identity() const { return driverId + QLatin1Char(':') + instanceId; }
    bool operator==(const DeviceConfiguration&) const = default;
};
struct PresetCollection {
    QList<LightingPreset> presets;
    QString defaultId;
    QList<ApplicationRule> rules;
    QList<EffectPreset> effects { EffectPreset() };
    std::optional<QList<DeviceConfiguration>> devices = std::nullopt;
    bool operator==(const PresetCollection&) const = default;
};
QList<EffectPreset> runtimeEffects(const PresetCollection& collection);
QString scopedEffectId(const LightingPreset& preset, const QString& id);
PresetCollection defaultCollection(const QString& deviceId = QStringLiteral("asus-g615lw"));
QSet<QString> referencedEffects(const LightingPreset& preset);
QJsonObject effectPresetJson(const EffectPreset& preset);
std::optional<EffectPreset> parseEffectPreset(const QByteArray& data, QString* error);
struct PresetBundle {
    LightingPreset keyboard;
    QList<EffectPreset> effects;
};
QJsonObject presetJson(const LightingPreset& preset);
std::optional<LightingPreset> parsePreset(const QByteArray& data, QString* error);
int matchingPreset(const PresetCollection& collection, const QString& windowClass);
int devicePreset(
    const PresetCollection& collection, const DeviceConfiguration& device, const QString& windowClass);
QString presetDirectory();
// The new index leaves old settings untouched. Loading never writes files.
std::optional<PresetCollection> loadPresets(const PresetCollection& fallback, QString* error);
// With expected, compare the previous snapshot while holding the publication lock.
bool savePresets(const PresetCollection& collection, QString* error,
    const PresetCollection* expected = nullptr, const PresetCollection* fallback = nullptr);
bool exportPreset(
    const LightingPreset& preset, const QList<EffectPreset>& effects, const QString& path, QString* error);
std::optional<PresetBundle> importPreset(const QString& path, QString* error);
bool exportEffectPreset(const EffectPreset& preset, const QString& path, QString* error);
std::optional<EffectPreset> importEffectPreset(const QString& path, QString* error);
}
