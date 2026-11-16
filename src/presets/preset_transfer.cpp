// SPDX-License-Identifier: GPL-3.0-or-later
#include "preset_transfer.h"
#include <QCoreApplication>
#include <algorithm>

namespace Ambient
{
namespace
{
    QMap<int, int> keyMapping(const DeviceInstance& from, const DeviceInstance& to)
    {
        QMap<int, int> result;
        const auto fromCodes = from.options.value("InputKeyCodes").toMap();
        const auto toCodes = to.options.value("InputKeyCodes").toMap();
        QSet<int> used;
        for (const auto& key : from.description.keys) {
            QList<int> candidates;
            const auto code = fromCodes.value(QString::number(key.id)).toUInt();
            if (code && from.description.id != to.description.id)
                for (const auto& target : to.description.keys)
                    if (toCodes.value(QString::number(target.id)).toUInt() == code)
                        candidates.append(target.id);
            if (candidates.isEmpty()) {
                const auto label = key.label.trimmed().toCaseFolded();
                const bool uniqueLabel = !label.isEmpty()
                    && std::count_if(from.description.keys.begin(), from.description.keys.end(),
                           [&](const auto& other) { return other.label.trimmed().toCaseFolded() == label; })
                        == 1;
                for (const auto& target : to.description.keys) {
                    const auto targetCode = toCodes.value(QString::number(target.id)).toUInt();
                    const bool match = from.description.id == to.description.id
                        ? key.id == target.id
                        : uniqueLabel && !(code && targetCode && code != targetCode)
                            && label == target.label.trimmed().toCaseFolded();
                    if (match)
                        candidates.append(target.id);
                }
            }
            // Ambiguous labels (e.g. two Shift keys) must not guess a side.
            if (candidates.size() == 1 && !used.contains(candidates.first())) {
                result.insert(key.id, candidates.first());
                used.insert(candidates.first());
            }
        }
        return result;
    }
}
ProfileTransfer transferProfile(const DeviceProfile& source, const DeviceInstance& from,
    const DeviceInstance& to, const QList<EffectPreset>& effects)
{
    ProfileTransfer result { source, { } };
    auto& p = result.profile;
    const auto& target = to.description;
    p.keyboardDeviceId = target.id;
    const bool perKey = target.capabilities.testFlag(DeviceCapability::PerKeyColor);
    const bool perStrip = target.capabilities.testFlag(DeviceCapability::PerStripColor);
    const bool rgb = target.capabilities.testFlag(DeviceCapability::Color);
    const bool brightness = rgb || target.capabilities.testFlag(DeviceCapability::Brightness);
    const bool ambient = brightness;
    QMap<int, int> mapping { { DefaultAssignment, DefaultAssignment } };
    if (perKey) {
        const auto keys = keyMapping(from, to);
        for (auto it = keys.cbegin(); it != keys.cend(); ++it)
            mapping.insert(it.key(), it.value());
    }
    if (perStrip || perKey) {
        for (int i = 0; i < std::min(from.description.strips.size(), target.strips.size()); ++i) {
            const auto& a = from.description.strips[i];
            const auto& b = target.strips[i];
            mapping.insert(a.id, b.id);
            if (perStrip)
                for (int j = 0; j < std::min(a.elements.size(), b.elements.size()); ++j)
                    mapping.insert(a.elements[j].id, b.elements[j].id);
        }
        if (from.description.strips.isEmpty() && target.strips.isEmpty()
            && from.description.capabilities.testFlag(DeviceCapability::Lightbar)
            && target.capabilities.testFlag(DeviceCapability::Lightbar))
            mapping.insert(LightbarId, LightbarId);
    }
    QSet<int> missing;
    auto remap = [&](auto& values) {
        auto next = values;
        next.clear();
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (mapping.contains(it.key()))
                next.insert(mapping[it.key()], it.value());
            else
                missing.insert(it.key());
        }
        values = std::move(next);
    };
    const auto remapGradients = [&](StripGradients& gradients) {
        StripGradients next;
        for (auto g : gradients)
            if (mapping.contains(g.strip) && mapping.contains(g.from) && mapping.contains(g.to)) {
                g.strip = mapping[g.strip];
                g.from = mapping[g.from];
                g.to = mapping[g.to];
                next.append(g);
            } else
                missing.insert(g.strip);
        gradients = std::move(next);
    };
    remap(p.base);
    remap(p.effects);
    remap(p.effectOverrides);
    remapGradients(p.gradients);
    for (auto& layer : p.layers) {
        remap(layer.styles);
        remap(layer.effects);
        remapGradients(layer.gradients);
    }
    for (auto it = p.groups.begin(); it != p.groups.end();) {
        QList<int> ids;
        for (int id : it.value())
            if (mapping.contains(id))
                ids.append(mapping[id]);
        if (ids.isEmpty())
            it = p.groups.erase(it);
        else {
            it.value() = ids;
            ++it;
        }
    }
    if (!missing.isEmpty())
        result.warnings.append(QCoreApplication::translate("Ambient::ProfileTransfer",
            "Assignments for %1 unavailable keys or strip elements will be "
            "skipped. Unmatched destination elements will use Base.")
                .arg(missing.size()));
    if (!rgb && from.description.capabilities.testFlag(DeviceCapability::Color))
        result.warnings.append(brightness && to.options.value("BrightnessSteps").toInt() > 1
                ? QCoreApplication::translate("Ambient::ProfileTransfer",
                      "Colors will be displayed as brightness using the destination device's brightness "
                      "source. Notification patterns are retained.")
                : QCoreApplication::translate("Ambient::ProfileTransfer",
                      "Only on/off lighting and notification flashes are available. Colors and "
                      "brightness levels cannot be reproduced."));
    QMap<QString, QString> types;
    for (const auto& effect : effects)
        types.insert(effect.id, effect.type);
    for (const auto& effect : source.localEffects)
        types.insert(effect.id, effect.type);
    bool removedAmbient = false, removedBrightness = false;
    const auto filterAssignments = [&](EffectAssignments& assignments) {
        for (auto& list : assignments)
            list.removeIf([&](const auto& item) {
                const auto type = types.value(item.id.isEmpty() ? source.defaultEffectId : item.id);
                if (!ambient && type == "ambient") {
                    removedAmbient = true;
                    return true;
                }
                if (!brightness && type == "brightness") {
                    removedBrightness = true;
                    return true;
                }
                return false;
            });
    };
    const auto filterStyles = [&](KeyboardStyles& styles) {
        for (auto& style : styles)
            if (!ambient && style.mode == KeyMode::Ambient) {
                style.mode = KeyMode::Static;
                style.effectId.clear();
                removedAmbient = true;
            }
    };
    if (!ambient && !p.defaultEffectId.isEmpty()) {
        removedAmbient |= !p.effects.contains(DefaultAssignment);
        p.defaultEffectId.clear();
        if (!p.effects.contains(DefaultAssignment))
            p.effects.insert(DefaultAssignment, { });
    }
    filterAssignments(p.effects);
    filterAssignments(p.effectOverrides);
    filterStyles(p.base);
    for (auto& layer : p.layers) {
        filterAssignments(layer.effects);
        filterStyles(layer.styles);
    }
    for (auto& g : p.gradients)
        if (!ambient && g.ambient) {
            g.ambient = false;
            g.effectId.clear();
            removedAmbient = true;
        }
    for (auto& layer : p.layers)
        for (auto& g : layer.gradients)
            if (!ambient && g.ambient) {
                g.ambient = false;
                g.effectId.clear();
                removedAmbient = true;
            }
    if (removedAmbient)
        result.warnings.append(QCoreApplication::translate("Ambient::ProfileTransfer",
            "Ambient will be omitted: this device needs color or at least four brightness levels plus Off."));
    if (removedBrightness)
        result.warnings.append(
            QCoreApplication::translate("Ambient::ProfileTransfer", "Brightness filters will be omitted."));
    auto references = p;
    references.localEffects.clear();
    const auto used = referencedEffects(references);
    p.localEffects.removeIf([&](const auto& effect) { return !used.contains(effect.id); });
    return result;
}
}
