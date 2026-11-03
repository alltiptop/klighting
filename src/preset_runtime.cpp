// SPDX-License-Identifier: GPL-3.0-or-later
#include "presets.h"
#include <QCoreApplication>
#include <algorithm>
#include <linux/input-event-codes.h>

namespace Ambient
{
QList<QStringList> presetLayerConditions(const LightingPreset& preset)
{
    if (preset.multiDevice)
        return preset.layerConditions;
    QList<QStringList> result;
    for (const auto& layer : preset.layers)
        result.append(layer.conditions);
    return result;
}
int activePresetLayer(const LightingPreset& preset, const ModifierState& modifiers)
{
    KeyboardLayers layers;
    for (const auto& conditions : presetLayerConditions(preset))
        layers.append({ conditions.join('+'), conditions, { } });
    return activeLayer(layers, modifiers);
}
bool mergePresetLayers(LightingPreset& preset)
{
    if (!preset.multiDevice)
        return true;
    auto conditions = preset.layerConditions;
    const auto indexOf = [&](const QStringList& value) {
        const QSet<QString> keys(value.cbegin(), value.cend());
        for (int i = 0; i < conditions.size(); ++i)
            if (keys == QSet<QString>(conditions[i].cbegin(), conditions[i].cend()))
                return i;
        return -1;
    };
    for (const auto& profile : preset.devices)
        for (const auto& layer : profile.layers)
            if (indexOf(layer.conditions) < 0)
                conditions.append(layer.conditions);
    if (conditions.size() > 32)
        return false;
    for (auto& profile : preset.devices) {
        KeyboardLayers layers;
        for (const auto& condition : conditions)
            layers.append({ condition.join('+'), condition, { } });
        for (const auto& layer : profile.layers) {
            const int index = indexOf(layer.conditions);
            layers[index] = layer;
            layers[index].conditions = conditions[index];
            layers[index].name = conditions[index].join('+');
        }
        profile.layers = std::move(layers);
    }
    preset.layerConditions = std::move(conditions);
    return true;
}
QList<QPair<QString, QString>> availableConditions()
{
    QList<QPair<QString, QString>> result {
        { "shift", QCoreApplication::translate("Ambient::KeyConditions", "Shift (Either / locked)") },
        { "shift-left", QCoreApplication::translate("Ambient::KeyConditions", "Shift (Left)") },
        { "shift-right", QCoreApplication::translate("Ambient::KeyConditions", "Shift (Right)") },
        { "ctrl", QCoreApplication::translate("Ambient::KeyConditions", "Ctrl (Either / locked)") },
        { "ctrl-left", QCoreApplication::translate("Ambient::KeyConditions", "Ctrl (Left)") },
        { "ctrl-right", QCoreApplication::translate("Ambient::KeyConditions", "Ctrl (Right)") },
        { "alt", QCoreApplication::translate("Ambient::KeyConditions", "Alt (Either / locked)") },
        { "alt-left", QCoreApplication::translate("Ambient::KeyConditions", "Alt (Left)") },
        { "alt-right", QCoreApplication::translate("Ambient::KeyConditions", "Alt (Right)") },
        { "super", QCoreApplication::translate("Ambient::KeyConditions", "Super (Either / locked)") },
        { "super-left", QCoreApplication::translate("Ambient::KeyConditions", "Super (Left)") },
        { "super-right", QCoreApplication::translate("Ambient::KeyConditions", "Super (Right)") },
        { "menu", QCoreApplication::translate("Ambient::KeyConditions", "Menu") },
        { "caps-lock", QCoreApplication::translate("Ambient::KeyConditions", "Caps Lock enabled") },
        { "fn", QCoreApplication::translate("Ambient::KeyConditions", "Fn (requires driver support)") },
        { "fn-lock", QCoreApplication::translate("Ambient::KeyConditions", "Fn Lock (unavailable)") }
    };
    const QList<QPair<int, QString>> keys { { KEY_A, "A" }, { KEY_B, "B" }, { KEY_C, "C" }, { KEY_D, "D" },
        { KEY_E, "E" }, { KEY_F, "F" }, { KEY_G, "G" }, { KEY_H, "H" }, { KEY_I, "I" }, { KEY_J, "J" },
        { KEY_K, "K" }, { KEY_L, "L" }, { KEY_M, "M" }, { KEY_N, "N" }, { KEY_O, "O" }, { KEY_P, "P" },
        { KEY_Q, "Q" }, { KEY_R, "R" }, { KEY_S, "S" }, { KEY_T, "T" }, { KEY_U, "U" }, { KEY_V, "V" },
        { KEY_W, "W" }, { KEY_X, "X" }, { KEY_Y, "Y" }, { KEY_Z, "Z" }, { KEY_1, "1" }, { KEY_2, "2" },
        { KEY_3, "3" }, { KEY_4, "4" }, { KEY_5, "5" }, { KEY_6, "6" }, { KEY_7, "7" }, { KEY_8, "8" },
        { KEY_9, "9" }, { KEY_0, "0" }, { KEY_F1, "F1" }, { KEY_F2, "F2" }, { KEY_F3, "F3" },
        { KEY_F4, "F4" }, { KEY_F5, "F5" }, { KEY_F6, "F6" }, { KEY_F7, "F7" }, { KEY_F8, "F8" },
        { KEY_F9, "F9" }, { KEY_F10, "F10" }, { KEY_F11, "F11" }, { KEY_F12, "F12" }, { KEY_ESC, "Esc" },
        { KEY_TAB, "Tab" }, { KEY_SPACE, QCoreApplication::translate("Ambient::KeyConditions", "Space") },
        { KEY_ENTER, "Enter" }, { KEY_BACKSPACE, "Backspace" }, { KEY_INSERT, "Insert" },
        { KEY_DELETE, "Delete" }, { KEY_HOME, "Home" }, { KEY_END, "End" },
        { KEY_PAGEUP, QCoreApplication::translate("Ambient::KeyConditions", "Page Up") },
        { KEY_PAGEDOWN, QCoreApplication::translate("Ambient::KeyConditions", "Page Down") },
        { KEY_UP, QCoreApplication::translate("Ambient::KeyConditions", "Up") },
        { KEY_DOWN, QCoreApplication::translate("Ambient::KeyConditions", "Down") },
        { KEY_LEFT, QCoreApplication::translate("Ambient::KeyConditions", "Left") },
        { KEY_RIGHT, QCoreApplication::translate("Ambient::KeyConditions", "Right") },
        { KEY_CAPSLOCK, QCoreApplication::translate("Ambient::KeyConditions", "Caps Lock key (held)") },
        { KEY_MINUS, "-" }, { KEY_EQUAL, "=" }, { KEY_LEFTBRACE, "[" }, { KEY_RIGHTBRACE, "]" },
        { KEY_SEMICOLON, ";" }, { KEY_APOSTROPHE, "'" }, { KEY_GRAVE, "`" },
        { KEY_BACKSLASH, QCoreApplication::translate("Ambient::KeyConditions", "Backslash") },
        { KEY_COMMA, "," }, { KEY_DOT, "." }, { KEY_SLASH, "/" } };
    for (const auto& [code, name] : keys)
        result.append({ QStringLiteral("key:%1").arg(code), name });
    return result;
}
QString conditionName(const QString& id)
{
    for (const auto& [key, name] : availableConditions())
        if (key == id)
            return name;
    return id;
}
bool conditionActive(const QString& c, const ModifierState& s)
{
    const auto down = [&s](quint32 code) { return s.pressed.contains(code); };
    if (c == "shift")
        return down(KEY_LEFTSHIFT) || down(KEY_RIGHTSHIFT) || s.effective.testFlag(Qt::ShiftModifier);
    if (c == "shift-left")
        return down(KEY_LEFTSHIFT);
    if (c == "shift-right")
        return down(KEY_RIGHTSHIFT);
    if (c == "ctrl")
        return down(KEY_LEFTCTRL) || down(KEY_RIGHTCTRL) || s.effective.testFlag(Qt::ControlModifier);
    if (c == "ctrl-left")
        return down(KEY_LEFTCTRL);
    if (c == "ctrl-right")
        return down(KEY_RIGHTCTRL);
    if (c == "alt")
        return down(KEY_LEFTALT) || down(KEY_RIGHTALT) || s.effective.testFlag(Qt::AltModifier)
            || s.effective.testFlag(Qt::GroupSwitchModifier);
    if (c == "alt-left")
        return down(KEY_LEFTALT);
    if (c == "alt-right")
        return down(KEY_RIGHTALT);
    if (c == "super")
        return down(KEY_LEFTMETA) || down(KEY_RIGHTMETA) || s.effective.testFlag(Qt::MetaModifier);
    if (c == "super-left")
        return down(KEY_LEFTMETA);
    if (c == "super-right")
        return down(KEY_RIGHTMETA);
    if (c == "menu")
        return down(KEY_COMPOSE) || down(KEY_MENU);
    if (c == "caps-lock")
        return s.capsLock;
    if (c == "fn")
        return down(KEY_FN);
    if (c.startsWith("key:")) {
        bool ok = false;
        const auto code = c.mid(4).toUInt(&ok);
        return ok && code > 0 && code <= KEY_MAX && down(code);
    }
    return false; // No reliable Fn Lock state is exposed by this laptop's driver.
}
bool layerActive(const KeyboardLayer& layer, const ModifierState& state)
{
    // Match modifier families exactly; ordinary typing keys may still be held.
    // Side-specific conditions are checked below, after excluding extra modifiers.
    for (const auto& modifier :
        { QStringLiteral("shift"), QStringLiteral("ctrl"), QStringLiteral("alt"), QStringLiteral("super") }) {
        const bool requested = std::any_of(layer.conditions.begin(), layer.conditions.end(),
            [&](const auto& c) { return c == modifier || c.startsWith(modifier + QLatin1Char('-')); });
        if (!requested && conditionActive(modifier, state))
            return false;
    }
    // Caps Lock substitutes only for a standalone, side-independent Shift layer.
    // Chords and side-specific modifiers still require their actual conditions.
    if (state.capsLock && layer.conditions == QStringList { QStringLiteral("shift") })
        return true;
    return !layer.conditions.isEmpty()
        && std::all_of(layer.conditions.begin(), layer.conditions.end(),
            [&](const auto& c) { return conditionActive(c, state); });
}
int activeLayer(const KeyboardLayers& layers, const ModifierState& state)
{
    int selected = -1;
    for (int i = 0; i < layers.size(); ++i)
        if (layerActive(layers[i], state)
            && (selected < 0 || layers[i].conditions.size() >= layers[selected].conditions.size()))
            selected = i;
    return selected;
}
KeyboardStyles resolveLayers(
    const KeyboardStyles& base, const KeyboardLayers& layers, const ModifierState& state)
{
    const int selected = activeLayer(layers, state);
    return selected < 0 ? base : effectiveKeyStyles(base, layers[selected].styles, true);
}
PresetCollection defaultCollection(const QString& deviceId)
{
    LightingPreset keyboard;
    keyboard.keyboardDeviceId = deviceId.isEmpty() ? QStringLiteral("unknown-device") : deviceId;
    return { { keyboard }, keyboard.id, { }, { EffectPreset() } };
}
KeyboardStyles expandStyles(
    const KeyboardStyles& styles, const StripGradients& gradients, const DeviceDescription& device)
{
    KeyboardStyles result;
    for (const auto& strip : device.strips) {
        if (styles.contains(strip.id) && styles.value(strip.id).mode != KeyMode::Inherit)
            for (const auto& element : strip.elements)
                result.insert(element.id, styles.value(strip.id));
        for (const auto& gradient : gradients) {
            if (gradient.strip != strip.id)
                continue;
            int from = -1, to = -1;
            for (int i = 0; i < strip.elements.size(); ++i) {
                if (strip.elements[i].id == gradient.from)
                    from = i;
                if (strip.elements[i].id == gradient.to)
                    to = i;
            }
            if (from < 0 || to < 0)
                continue;
            for (int i = std::min(from, to); i <= std::max(from, to); ++i) {
                const double t = from == to ? 0 : double(i - from) / (to - from);
                const auto mix = [t](int a, int b) { return qRound(a + (b - a) * t); };
                const auto& a = gradient.start;
                const auto& b = gradient.end;
                const QColor color(mix(a.red(), b.red()), mix(a.green(), b.green()), mix(a.blue(), b.blue()),
                    mix(a.alpha(), b.alpha()));
                result.insert(strip.elements[i].id,
                    { gradient.ambient ? KeyMode::Ambient : KeyMode::Static, color, gradient.effectId });
            }
        }
    }
    return effectiveKeyStyles(result, styles, true);
}
namespace
{
    QList<int> deviceElements(const DeviceDescription& device)
    {
        QList<int> ids;
        for (const auto& key : device.keys)
            ids.append(key.id);
        for (const auto& strip : device.strips) {
            ids.append(strip.id);
            for (const auto& element : strip.elements)
                ids.append(element.id);
        }
        if (ids.isEmpty() || device.capabilities.testFlag(DeviceCapability::Lightbar))
            ids.append(LightbarId);
        return ids;
    }
    EffectAssignments expandedEffects(
        const EffectAssignments& assignments, const KeyboardStyles& styles, const DeviceDescription& device)
    {
        EffectAssignments result;
        if (assignments.contains(DefaultAssignment))
            for (int id : deviceElements(device))
                if (!styles.contains(id) || styles[id].mode == KeyMode::Inherit)
                    result.insert(id, assignments[DefaultAssignment]);
        for (const auto& strip : device.strips)
            if (assignments.contains(strip.id))
                for (const auto& element : strip.elements)
                    result.insert(element.id, assignments[strip.id]);
        for (auto it = assignments.cbegin(); it != assignments.cend(); ++it)
            if (it.key() != DefaultAssignment)
                result.insert(it.key(), it.value());
        return result;
    }
    KeyboardStyles stylesWithDefault(const KeyboardStyles& input, const StripGradients& gradients,
        const EffectAssignments& assignments, const DeviceDescription& device)
    {
        auto styles = expandStyles(input, gradients, device);
        if (assignments.contains(DefaultAssignment))
            for (int id : deviceElements(device))
                if (!styles.contains(id) || styles[id].mode == KeyMode::Inherit)
                    styles.insert(id, { KeyMode::Static, Qt::transparent });
        return styles;
    }
}
KeyboardStyles deviceKeyStyles(
    const DeviceProfile& preset, const DeviceDescription& device, const ModifierState& modifiers)
{
    return deviceKeyStylesForLayer(preset, device, activeLayer(preset.layers, modifiers));
}
KeyboardStyles deviceKeyStylesForLayer(
    const DeviceProfile& preset, const DeviceDescription& device, int selected)
{
    if (preset.keyboardDeviceId != device.id
        || (!device.capabilities.testFlag(DeviceCapability::PerKeyColor)
            && !device.capabilities.testFlag(DeviceCapability::PerStripColor)))
        return { };
    auto result = stylesWithDefault(preset.base, preset.gradients, preset.effects, device);
    if (selected >= 0 && selected < preset.layers.size()) {
        const auto& layer = preset.layers[selected];
        result = effectiveKeyStyles(
            result, stylesWithDefault(layer.styles, layer.gradients, layer.effects, device), true);
    }
    return result;
}
EffectList implicitEffects(const KeyStyle& style)
{
    if (style.mode == KeyMode::Ambient)
        return { { style.effectId } };
    return { };
}
QString scopedEffectId(const DeviceProfile& preset, const QString& id)
{
    for (const auto& effect : preset.localEffects)
        if (effect.id == id)
            return preset.id + QLatin1Char(':') + id;
    return id;
}
QList<EffectPreset> runtimeEffects(const PresetCollection& collection)
{
    auto result = collection.effects;
    for (const auto& scene : collection.presets)
        for (const auto* preset : deviceProfiles(scene))
            for (auto effect : preset->localEffects) {
                effect.id = scopedEffectId(*preset, effect.id);
                result.append(effect);
            }
    return result;
}
EffectAssignments deviceEffects(
    const DeviceProfile& p, const DeviceDescription& d, const ModifierState& modifiers)
{
    return deviceEffectsForLayer(p, d, activeLayer(p.layers, modifiers));
}
EffectAssignments deviceEffectsForLayer(const DeviceProfile& p, const DeviceDescription& d, int selected)
{
    if (p.keyboardDeviceId != d.id)
        return { };
    if (!d.capabilities.testFlag(DeviceCapability::PerKeyColor)
        && !d.capabilities.testFlag(DeviceCapability::PerStripColor)) {
        EffectAssignments result;
        const auto& assignments = selected >= 0 && selected < p.layers.size()
                && p.layers[selected].effects.contains(DefaultAssignment)
            ? p.layers[selected].effects
            : p.effects;
        if (assignments.contains(DefaultAssignment))
            for (int id : deviceElements(d))
                result.insert(id, assignments[DefaultAssignment]);
        const auto overrides = p.effectOverrides.value(DefaultAssignment);
        if (!overrides.isEmpty())
            for (int id : deviceElements(d)) {
                auto list = result.value(
                    id, implicitEffects(KeyStyle { KeyMode::Ambient, Qt::transparent, p.defaultEffectId }));
                list.append(overrides);
                result.insert(id, list);
            }
        for (auto& list : result)
            for (auto& effect : list)
                effect.id = scopedEffectId(p, effect.id);
        return result;
    }
    auto result = expandedEffects(p.effects, expandStyles(p.base, p.gradients, d), d);
    if (selected >= 0 && selected < p.layers.size()) {
        const auto& layer = p.layers[selected];
        const auto styles = expandStyles(layer.styles, layer.gradients, d);
        for (auto it = styles.cbegin(); it != styles.cend(); ++it)
            if (it->mode != KeyMode::Inherit)
                result.insert(it.key(), implicitEffects(it.value()));
        const auto effects = expandedEffects(layer.effects, styles, d);
        for (auto it = effects.cbegin(); it != effects.cend(); ++it)
            result.insert(it.key(), it.value());
    }
    const auto styles = deviceKeyStylesForLayer(p, d, selected);
    const auto overrides = expandedEffects(p.effectOverrides, { }, d);
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
        auto list = result.value(it.key(), implicitEffects(keyStyle(styles, it.key())));
        list.append(it.value());
        result.insert(it.key(), list);
    }
    if (d.capabilities.testFlag(DeviceCapability::PerStripColor))
        for (const auto& strip : d.strips)
            result.remove(strip.id);
    const auto supported = deviceElements(d);
    for (auto it = result.begin(); it != result.end();) {
        if (!supported.contains(it.key()))
            it = result.erase(it);
        else {
            for (auto& effect : it.value())
                effect.id = scopedEffectId(p, effect.id);
            ++it;
        }
    }
    return result;
}
QSet<QString> referencedEffects(const DeviceProfile& p)
{
    QSet<QString> ids;
    if (!p.effects.contains(DefaultAssignment) && !p.defaultEffectId.isEmpty())
        ids.insert(p.defaultEffectId);
    auto add = [&](const KeyboardStyles& styles) {
        for (const auto& style : styles)
            if (style.mode == KeyMode::Ambient)
                ids.insert(style.effectId.isEmpty() ? p.defaultEffectId : style.effectId);
    };
    auto addAssignments = [&](const EffectAssignments& assignments) {
        for (const auto& list : assignments)
            for (const auto& effect : list)
                if (effect.id != QLatin1String(InlineColorId))
                    ids.insert(effect.id.isEmpty() ? p.defaultEffectId : effect.id);
    };
    addAssignments(p.effects);
    addAssignments(p.effectOverrides);
    const auto addGradients = [&](const StripGradients& gradients) {
        for (const auto& g : gradients)
            if (g.ambient)
                ids.insert(g.effectId.isEmpty() ? p.defaultEffectId : g.effectId);
    };
    addGradients(p.gradients);
    add(p.base);
    for (const auto& layer : p.layers) {
        add(layer.styles);
        addGradients(layer.gradients);
        addAssignments(layer.effects);
    }
    for (const auto& effect : p.localEffects)
        ids.remove(effect.id);
    ids.remove(QString());
    return ids;
}
QList<const DeviceProfile*> deviceProfiles(const LightingPreset& preset)
{
    if (!preset.multiDevice)
        return { &preset };
    QList<const DeviceProfile*> result;
    for (const auto& profile : preset.devices)
        result.append(&profile);
    return result;
}
QList<DeviceProfile*> deviceProfiles(LightingPreset& preset)
{
    if (!preset.multiDevice)
        return { &preset };
    QList<DeviceProfile*> result;
    for (auto& profile : preset.devices)
        result.append(&profile);
    return result;
}
const DeviceProfile* profileFor(const LightingPreset& preset, const DeviceConfiguration& device)
{
    for (const auto* profile : deviceProfiles(preset))
        if (profile->keyboardDeviceId == device.modelId
            && (profile->targetDevice.isEmpty() || profile->targetDevice == device.identity()))
            return profile;
    return nullptr;
}
QSet<QString> referencedEffects(const LightingPreset& preset)
{
    QSet<QString> result;
    for (const auto* profile : deviceProfiles(preset))
        result.unite(referencedEffects(*profile));
    return result;
}
int matchingPreset(const PresetCollection& c, const QString& windowClass)
{
    const auto find = [&c](const QString& id) {
        for (int i = 0; i < c.presets.size(); ++i)
            if (c.presets[i].id == id)
                return i;
        return -1;
    };
    if (!windowClass.isEmpty())
        for (const auto& r : c.rules)
            if (r.enabled && !r.windowClass.trimmed().isEmpty()
                && r.windowClass.trimmed().toLower() == windowClass.toLower()) {
                const int index = find(r.presetId);
                if (index >= 0)
                    return index;
            }
    const int fallback = find(c.defaultId);
    return fallback >= 0 ? fallback : (c.presets.isEmpty() ? -1 : 0);
}
int devicePreset(const PresetCollection& c, const DeviceConfiguration& device, const QString& windowClass)
{
    const auto find = [&](const QString& id) {
        for (int i = 0; i < c.presets.size(); ++i)
            if (c.presets[i].id == id && profileFor(c.presets[i], device))
                return i;
        return -1;
    };
    if (!windowClass.isEmpty())
        for (const auto& rule : c.rules)
            if (rule.enabled && !rule.windowClass.trimmed().isEmpty()
                && rule.windowClass.trimmed().compare(windowClass, Qt::CaseInsensitive) == 0) {
                const int i = find(rule.presetId);
                if (i >= 0)
                    return i;
            }
    return find(c.defaultId);
}
}
